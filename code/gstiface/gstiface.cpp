# include "./code/gstiface/gstiface.h"
# include "./code/playerctl/playerctl.h"	

# include <gst/video/videooverlay.h>

# include "./code/resource.h"

# include <QDebug>
# include <QWidget>
# include <QTime>

//  Helper function: Return TRUE if this is a Visualization element 
static gboolean filter_vis_features (GstPluginFeature *feature, gpointer data) {
	
  GstElementFactory* factory;
   
  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;
  factory = GST_ELEMENT_FACTORY (feature);
  if (!g_strrstr (gst_element_factory_get_klass (factory), "Visualization"))
    return FALSE;
   
  return TRUE;
}

// Constructor
GST_Interface::GST_Interface(QObject* parent) : QObject(parent)
{
	// members
	vismap.clear();
	streammap.clear();
	mainwidget = qobject_cast<QWidget*>(parent);
	b_positionenabled = true;	// this will be set to false if we fail getting the stream position.
														// Checked at the top of queryStreamPosition so we don't flood the logs
														// or the display with failed position messages
	
	// setup the dialog to display stream info
	streaminfo = new StreamInfo(this, mainwidget);
	streaminfo->enableAll(false);
		
	// initialize gstreamer
	gst_init(NULL, NULL);
	
	// Create the playbin pipeline, call it player
	pipeline = gst_element_factory_make("playbin", PLAYER_NAME);
	
	// Create the playbin bus
	bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));	

	// Start a timer to monitor the bus
  timer = new QTimer(this);
  connect(timer, SIGNAL(timeout()), this, SLOT(pollGstBus()));
  timer->start(500); // time measured in miliseconds  
  
  // Create a QMap of the available audio visualizers.  Map format is
  // QString key
  // GstElementFactory* value
  GList* list;
  GList* walk;
 
  // Get a list of all visualization plugins.  Use the helper function
  // filter_vis_features() at the top of this file.
  list = gst_registry_feature_filter (gst_registry_get(),filter_vis_features, FALSE, NULL);
  
  // Walk through each visualizer plugin looking for visualizers
  for (walk = list; walk != NULL; walk = g_list_next (walk)) {
    const gchar* name;
    GstElementFactory* factory;
     
    factory = GST_ELEMENT_FACTORY (walk->data);
    name = gst_element_factory_get_longname (factory);
    vismap[name] = factory;
  }	// for

	// clean up
  g_list_free(list);
  g_list_free(walk);
}

// Destructor
GST_Interface::~GST_Interface()
{
	gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));
  gst_object_unref (bus);
  delete timer;
}

///////////////////////////// Public Functions /////////////////////////
void GST_Interface::playMedia(WId winId, const QString& uri)
{
	// start with the pipeline set to READY
	gst_element_set_state (pipeline, GST_STATE_READY);

	// Set the media source
	g_object_set(G_OBJECT(pipeline), "uri", qPrintable(uri), NULL);

	// Set the video overlay
	gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(pipeline), winId);

	// Start the playback
	gst_element_set_state(pipeline, GST_STATE_PLAYING);

  return;
}

//
// Toggle between pause and play.  The actual slot that receives playPause
// signals is in playerctl.  That slot calls this as a plain function.
void GST_Interface::playPause()
{
	GstState state = getState();
	
	if (state == GST_STATE_PLAYING) 
		gst_element_set_state(pipeline, GST_STATE_PAUSED);
	
	if (state == GST_STATE_PAUSED)
		gst_element_set_state(pipeline, GST_STATE_PLAYING);
	
	return;
}

//
// Return the pipeline state (null, ready, paused, playing)
GstState GST_Interface::getState()
{
	// constants
	const guint timeout = 500;		// timeout in miliseconds
	
	// variables
	GstState state;
	
	// get the state and return it
	gst_element_get_state(pipeline, &state, NULL, timeout);
	return state;
}

//
// Function to return the software volume.  Allowed values are 0-10
// 0.0 = mute, 1.0 = 100%, so 10 must be really loud.  The volume
// scale is linear
double GST_Interface::getVolume()
{
	//variables
	gdouble vol = 0.0;
	
	// retrieve the volume and return it
	g_object_get (G_OBJECT (pipeline), "volume", &vol, NULL);
	return vol;
}   

//
// Function to return a QStringList of the visualizers found
QList<QString> GST_Interface::getVisualizerList()
{
	return vismap.keys();
}


//
// Function to change the visualizer.  Called as a function from a
// slot in the playerctl class
void GST_Interface::changeVisualizer(const QString& vis)
{
	GstElement* vis_plugin = NULL;
	GstElementFactory* selected_factory = NULL;
	
	// this should not fail, vis and the selected_factory should both
	// exist in the vismap, and the function that sends us here gets 
	// the vis string from the map, but just in case
	selected_factory = vismap.value(vis);
	if (!selected_factory) {
		gst_element_post_message (pipeline,
			gst_message_new_application (GST_OBJECT (pipeline),
				gst_structure_new ("Application", "MBMP", G_TYPE_STRING, "Error: No visualization plugins found", NULL)));  
		return ;
	}	// if
  
  // We have now selected a factory for the visualization element 
  vis_plugin = gst_element_factory_create (selected_factory, NULL);
  if (!vis_plugin) vis_plugin = NULL;	// if null use the default
	
	//set the vis plugin for our pipeline
  g_object_set (pipeline, "vis-plugin", vis_plugin, NULL); 
}

//
//	Function to check the setting of a GstPlayFlag. Send the flag
// to be checked, return true if set and false if unset
bool GST_Interface::checkPlayFlag(const guint& checkflag)
{
	// variables
	guint flags = 0;
	g_object_get (pipeline, "flags", &flags, NULL);
	
	// check if set
	return (flags & checkflag);
}

//
//	Function to set or unset a GstPlayFlag.  Send the flag
//	to operate on and a bool setting, true to set, false to unset
void GST_Interface::setPlayFlag (const guint& targetflag, const bool& b)
{
	// variables
	guint flags = 0;
	g_object_get (pipeline, "flags", &flags, NULL);
	
	// now do the setting or unsetting
	b ? flags |= targetflag : flags &= ~targetflag;
	g_object_set (pipeline, "flags", flags, NULL);	
}

//
//	Function to create and return a QString about the audio stream
// properties
QString GST_Interface::getAudioStreamInfo()
{
	QString s = QString();
	GstTagList* tags;
	gchar* str;
  guint rate;
  
  // if no audio streams were found
  if (streammap["n-audio"] < 1)
		s.append(tr("No Audio Streams Found"));
		
	// else for each audio stream found
  else {	
	  for (int i = 0; i < streammap["n-audio"]; i++) {
			tags = NULL;
	
			// Emit the get-audio-tags signal, store and process the stream's audio tags 
			g_signal_emit_by_name (pipeline, "get-audio-tags", i, &tags);
			if (tags) {
				if (i == streammap["current-audio"] ) s.append("<b>");
				s.append(tr("Audio Stream: %1<br>").arg(i));
				if (gst_tag_list_get_string (tags, GST_TAG_AUDIO_CODEC, &str)) {
					s.append(tr("Codec: %1<br>").arg(str) );
					g_free (str);
				}
				
				if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
					s.append(tr("Language: %1<br>").arg(str) );
					g_free (str);
				}
				
				if (gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &rate)) {
					s.append(tr("Bitrate: %1<br>").arg(rate));
				}
				
				// While we are here extract some media tags if present.
				// Set the title of the main player window using the tags
				// or our fallback string if tags are not found.
				QString qs_title = 0;
				QString qs_artist = 0;
				if (gst_tag_list_get_string (tags, GST_TAG_TITLE, &str)) {
					if (str) qs_title = QString(str);
					else qs_title.clear();
					g_free (str);
				}
				
				if (gst_tag_list_get_string (tags, GST_TAG_ARTIST, &str)) {
					if (str) qs_artist = QString(str);
					else qs_artist.clear();
					g_free (str);
				}
				if (qs_title.isEmpty() ) mainwidget->setWindowTitle(WINDOW_TITLE);
				else mainwidget->setWindowTitle(QString("%1 - %2").arg(qs_title).arg(qs_artist) );
				
				// side trip to window title is over, now back to business
				s.append("<br>");
				gst_tag_list_free (tags);
				if (i == streammap["current-audio"] ) s.append("</b>");
			}	// if tags were found
		}	// for loop
	}	// else
	
	return s;
}

//
//	Function to create and return a QString about the video stream
// properties
QString GST_Interface::getVideoStreamInfo()
{
	QString s = QString();
	GstTagList* tags;
	gchar* str;
    
  // if no video streams were found
  if (streammap["n-video"] < 1)
		s.append(tr("No Video Streams Found"));
  
	// else for each video stream found
  else {	
	  for (int i = 0; i < streammap["n-video"]; i++) {
			tags = NULL;
    
			// Emit the get-video-tags signal, store and process the stream's video tags 
			g_signal_emit_by_name (pipeline, "get-video-tags", i, &tags);
			if (tags) {
				if (i == streammap["current-video"] ) s.append("<b>");
				s.append(tr("Video Stream: %1<br>").arg(i));			
      
				gst_tag_list_get_string (tags, GST_TAG_VIDEO_CODEC, &str);
				s.append(tr("Codec: %1<br>").arg(str) );
      
				s.append("<br>");
				g_free (str);
				gst_tag_list_free (tags);
				if (i == streammap["current-video"] ) s.append("</b>");
				}	// if tags
			}	// for				
	}	// else

	return s;
}

//
//	Function to create and return a QString about the text stream
// properties
QString GST_Interface::getTextStreamInfo()
{
	QString s = QString();
	GstTagList* tags;
	gchar* str;
    
  // if no subtitle streams were found
  if (streammap["n-text"] < 1)
		s.append(tr("No Subtitle Streams Found"));
  
	// else for each subtitle stream found
  else {	
	  for (int i = 0; i < streammap["n-text"]; i++) {
			tags = NULL;
    
			// Emit the get-text-tags signal, store and process the stream's text tags 
			g_signal_emit_by_name (pipeline, "get-text-tags", i, &tags);
			if (tags) {
				if (i == streammap["current-text"] ) s.append("<b>");
				s.append(tr("Subtitle Stream: %1<br>").arg(i));			
      
        if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
					s.append(tr("Language: %1<br>").arg(str ? str : tr("unknown")));
					g_free (str);
				}
				s.append("<br>");
				gst_tag_list_free (tags);
				if (i == streammap["current-text"] ) s.append("</b>");
			}	// if tags
			else
				s.append(tr("No subtitle tags found"));
		}	// for				
	}	// else

	return s;	
}

//////////////////////////// Public Slots ////////////////////////////
//
// Slot to query an audio CD and play tracks
void GST_Interface::playCD()
{
	qDebug() << "start CD";
	
	// put the pipeline in ready
	gst_element_set_state(pipeline, GST_STATE_READY);
	
}


//
// Slot to seek to a specific position in the stream. Seek position
// sent is in seconds so need to convert it to nanoseconds for gstreamer
// Called when a QAction is triggered
void GST_Interface::seekToPosition(int position)
{
	// return if seeking is not enabled
	if (! this->queryStreamSeek() ) return;
	
	// seek flags
	const int seekflags =  GST_SEEK_FLAG_FLUSH 		|	// flush the pipeline
													GST_SEEK_FLAG_SKIP 			|	// allow skipping frames
													GST_SEEK_FLAG_KEY_UNIT	;	// seek to the nearest keyframe, faster but maybe not as accurate
	
	// now do the seek
	gst_element_seek_simple(pipeline, GST_FORMAT_TIME, (GstSeekFlags)(seekflags) , position * GST_SECOND);
	return;
} 


//
// Slot to change the audio stream to the stream number sent
void GST_Interface::setAudioStream(const int& stream)
{
	// make sure the stream exists
	if (stream < 0 || stream >= (streammap["n-audio"]) ) return;
			
	// change the stream to the int sent to the function
	g_object_set (G_OBJECT (pipeline), "current-audio", stream, NULL);
		
	// update the streammap and then the text display boxes
	streammap["current-audio"] = stream;
	streaminfo->updateAudioBox(getAudioStreamInfo());
		
	return;
}	

//
// Slot to change the video stream to the stream number sent
void GST_Interface::setVideoStream(const int& stream)
{
	// make sure the stream exists
	if (stream < 0 || stream >= (streammap["n-video"]) ) return;
			
	// change the stream to the int sent to the function
	g_object_set (G_OBJECT (pipeline), "current-video", stream, NULL);
		
	// update the streammap and text display boxes
	streammap["current-video"] = stream;
	streaminfo->updateVideoBox(getVideoStreamInfo());
	
	return;
}	

//
// Slot to change the subtitle stream to the stream number sent
void GST_Interface::setTextStream(const int& stream)
{
	// make sure the stream exists
	if (stream < 0 || stream >= (streammap["n-text"]) ) return;
	
	// change the stream to the int sent to the function
	g_object_set (G_OBJECT (pipeline), "current-text", stream, NULL);
	
	// update the streammap and text display boxes
	streammap["current-text"] = stream;
	streaminfo->updateSubtitleBox(getTextStreamInfo());
	
	return;
}	

	
// Poll the bus and emit signals for messages we choose to deal with.
// Called by a QTimer in the constructor.  Do minimal processing here
// and emit the busMessage signal for PlayerControl::processBusMessage
// to pickup and complete the processing.  Basically anything that needs
// to operate immediately on the stream do here, anything that the user
// needs to know about do in PlayerControl.
void GST_Interface::pollGstBus()
{
	// query the stream position if we are currently playing. This will
	// set the PlayerControl position widgets
	if (getState() == GST_STATE_PLAYING) queryStreamPosition();
	
	// return if there are no new messages
	if (! gst_bus_have_pending(bus)) return;
	
	
	// variables and constants
	GstMessage* msg = 0;	
	const int msgtypes = GST_MESSAGE_EOS 							|
												GST_MESSAGE_ERROR							| 
												GST_MESSAGE_WARNING 					|
												GST_MESSAGE_INFO							|
												GST_MESSAGE_STATE_CHANGED			|
												GST_MESSAGE_STREAM_START			|
												GST_MESSAGE_APPLICATION				|
												GST_MESSAGE_BUFFERING					|	
												GST_MESSAGE_DURATION_CHANGED	|
												GST_MESSAGE_TOC								|
												GST_MESSAGE_CLOCK_LOST;
	
	msg = (GstMessage*)(gst_bus_pop_filtered(bus, (GstMessageType)(msgtypes)) );
	while (msg != NULL) {		
		switch (GST_MESSAGE_TYPE (msg)) {
			
			// An ERROR message generated somewhere in the pipeline.  Gstreamer docs say the pipeline should be taken out down if an ERROR
			// message is sent, however I've found this does not seem to be the behavior when using the commandline gst-launch-1.0 utility.
			// I've remove the command to take down the pipeline from here to match that behavior. 
			case GST_MESSAGE_ERROR: {
				GError* err = NULL;
				gchar* dbg_info = NULL;
				
				gst_message_parse_error (msg, &err, &dbg_info);
				emit busMessage(MBMP::Error, QString(tr("ERROR from element %1: %2\n  Debugging information: %3\n  The pipeline has been shut down"))
						.arg(GST_OBJECT_NAME (msg->src))
						.arg(err->message)
						.arg( (dbg_info) ? dbg_info : "none") );
				
				g_error_free (err);
				g_free (dbg_info);		
				break; }
		
		// A WARNING message generated somewhere in the pipeline
			case GST_MESSAGE_WARNING: {
				GError* err = NULL;
				gchar* dbg_info = NULL;
				
				gst_message_parse_warning (msg, &err, &dbg_info);
				emit busMessage(MBMP::Warning, QString(tr("WARNING MESSAGE from element %1: %2\n  Debugging information: %3"))
						.arg(GST_OBJECT_NAME (msg->src))
						.arg(err->message)
						.arg( (dbg_info) ? dbg_info : "none") );
				
				g_error_free (err);
				g_free (dbg_info);		
				break; }
			
			// An INFO message generated somewhere in the pipeline
			case GST_MESSAGE_INFO: {
				GError* err = NULL;
				gchar* dbg_info = NULL;
				
				gst_message_parse_info (msg, &err, &dbg_info);
				emit busMessage(MBMP::Info, QString(tr("INFOMATION MESSAGE from element %1: %2\n  Debugging information: %3"))
						.arg(GST_OBJECT_NAME (msg->src))
						.arg(err->message)
						.arg( (dbg_info) ? dbg_info : "none") );
				
				g_error_free (err);
				g_free (dbg_info);		
				break; }
			
			// A clock_lost message, try to reset the clock by pausing then restarting the player
			case GST_MESSAGE_CLOCK_LOST	: {
				emit busMessage(MBMP::ClockLost, QString(tr("Pipeline clock has become unusable, trying to reset...")) );
				gst_element_set_state (pipeline, GST_STATE_PAUSED);
				gst_element_set_state (pipeline, GST_STATE_PLAYING);
				break;	}
	
			// The end of stream message. Put the player into the READY state.m
			case GST_MESSAGE_EOS: {
				emit busMessage(MBMP::EOS, QString(tr("End of stream has been reached.")) );
				gst_element_set_state (pipeline, GST_STATE_READY);
				break; }
			
			// The start of stream message
			case GST_MESSAGE_STREAM_START: {
				emit busMessage(MBMP::SOS, QString(tr("Start of a stream has been detected.")) );
				break; }
			
			// Player state changed.  Do a bunch of processing here to analyze the stream and
			// set the streaminfo dialog accordingly.
			case GST_MESSAGE_STATE_CHANGED: {
		    GstState old_state;
				GstState new_state;
		    
		    gst_message_parse_state_changed (msg, &old_state, &new_state, NULL);
				emit busMessage(MBMP::State, QString(tr("%1 has changed state from %2 to %3."))
																						.arg(GST_OBJECT_NAME (msg->src))
																						.arg(gst_element_state_get_name (old_state))
																						.arg(gst_element_state_get_name (new_state)) ); 
				// set the streammap based on what state changed																																							
				if (QString(GST_OBJECT_NAME (msg->src)).contains(PLAYER_NAME, Qt::CaseSensitive)) {																		 	
					switch (new_state) {
						case GST_STATE_PLAYING:
							analyzeStream();
							streaminfo->updateAudioBox(getAudioStreamInfo());
							streaminfo->updateVideoBox(getVideoStreamInfo());
							streaminfo->updateSubtitleBox(getTextStreamInfo());
							streaminfo->setComboBoxes(streammap);	
							streaminfo->setSubtitleBoxEnabled(checkPlayFlag(GST_PLAY_FLAG_TEXT));
							streaminfo->enableAll(true);
							qobject_cast<PlayerControl*>(mainwidget)->setDurationWidgets(queryDuration() / (1000 * 1000 * 1000), queryStreamSeek() );
							break;
						case GST_STATE_PAUSED:
							streaminfo->enableAll(false);
							break;
						default:
							streaminfo->updateAudioBox(tr("Audio Information"));
							streaminfo->updateVideoBox(tr("Video Information"));
							streaminfo->updateSubtitleBox(tr("Subtitle Information"));
							streammap.clear();
							streaminfo->setComboBoxes(streammap);	
							streaminfo->enableAll(false);
							qobject_cast<PlayerControl*>(mainwidget)->setDurationWidgets(-1);
							b_positionenabled = true;
					}	// state switch
				}	// if						
				break; }    
			
			// A message we generate
			case GST_MESSAGE_APPLICATION: {
				gchar* payload = NULL;
				gst_structure_get(gst_message_get_structure(msg), "MBMP", G_TYPE_STRING, &payload, NULL); 
				emit busMessage(MBMP::Application, QString(payload));
				g_free(payload);
				break; }
			
			// Buffering messages, pause the playback while buffering, restart when finished
			case GST_MESSAGE_BUFFERING: {
				gint percent = 0;
				gst_message_parse_buffering(msg, &percent);
				percent < 100 ? gst_element_set_state (pipeline, GST_STATE_PAUSED) : gst_element_set_state (pipeline, GST_STATE_PLAYING);
				emit busMessage(MBMP::Buffering, QString::number(percent));
				break; }
					
			// Duration changed message.  These are typically only created for streams that have a variable bit rate
			// where the pipeline calculates a duration based on some average bitrate.  Only report the duration changed
			// using the emit, we activate or disactivate the position widgets from the STATE_CHANGED case above. 		
			case GST_MESSAGE_DURATION_CHANGED: {
				QTime t(0,0,0);
				t = t.addSecs(queryDuration() / (1000 * 1000 * 1000));
				emit busMessage(MBMP::Duration, QString(tr("New stream duration: %1")).arg(t.toString("HH:mm:ss")) );
				break; }
				
			// TOC message, for instance from an audio CD or DVD 
			case GST_MESSAGE_TOC: {
				GstToc* toc = 0;
				gboolean updated = false;
				
				gst_message_parse_toc (msg, &toc, &updated);
				GList* entry = gst_toc_get_entries(toc);				
				
				
			qDebug() << "list size: " << g_list_length(entry);

	
	
	
				g_list_free(entry);				
				gst_toc_unref(toc);

				break;
			}	
				
			default:
				emit busMessage(MBMP::Unhandled, QString(tr("Unhandled GSTBUS message")) );
		}	// GST_MESSAGE switch
			
		gst_message_unref(msg);
		msg = (GstMessage*)(gst_bus_pop_filtered(bus, (GstMessageType)(msgtypes)) );		
	}	// while loop 
		
	return;
}

// Slot to toggle mute
void GST_Interface::toggleMute()
{
	// variables
	gboolean b_mute = false;
	
	// toggle the mute setting
	g_object_get (G_OBJECT (pipeline), "mute", &b_mute, NULL);	 
	b_mute ? g_object_set (G_OBJECT (pipeline), "mute", false, NULL) : g_object_set (G_OBJECT (pipeline), "mute", true, NULL);
		
	return;
}

// Slot to change the volume.  Volume needs to be in the 0.0 to 10.0
// with a default of 1.0. This is checked in the calling function
void GST_Interface::changeVolume(const double& d_vol)
{
	// change the volume to the double we sent to the function
	g_object_set (G_OBJECT (pipeline), "volume", d_vol, NULL);
	
	return;
}

// Slot to change the connection speed.  Speed is measured in kbps
// and needs to be <= 18446744073709551.  Default is 0
void GST_Interface::changeConnectionSpeed(const guint64& ui64_speed)
{	
	// change the connection soeed to the ui64 sent to the function
	g_object_set (G_OBJECT (pipeline), "connection-speed", ui64_speed, NULL);
		
	return;
}

// Slot to stop the player
void GST_Interface::playerStop()
{
	gst_element_set_state (pipeline, GST_STATE_READY);
	
	return;
}

//
// Slot to toggle the streaminfo dialog up and down.  Called from
// a QAction in various functions
void GST_Interface::toggleStreamInfo()
{
	streaminfo->isVisible() ? streaminfo->hide() : streaminfo->show();
	
	return;
}

//////////////////////////// Private Functions//////////////////////////
//
// Function to query the media stream and fillout the streammap.  Called
// from pollGstBus whenever the state of the player changes to PLAYING.
// Ignore all the other pipeline subobjects going into the PLAYING state.
void GST_Interface::analyzeStream()
{
	int target = 0;
	streammap.clear();
	
	g_object_get (pipeline, "n-video", &target, NULL);
	streammap["n-video"] = target;
  g_object_get (pipeline, "n-audio", &target, NULL);
  streammap["n-audio"] = target;
  g_object_get (pipeline, "n-text", &target, NULL);
  streammap["n-text"] = target;
	g_object_get (pipeline, "current-video", &target, NULL);
	streammap["current-video"] = target;
  g_object_get (pipeline, "current-audio", &target, NULL);
  streammap["current-audio"] = target;
  g_object_get (pipeline, "current-text", &target, NULL);
  streammap["current-text"] = target;

  return;
}

//
// Function to query the steam position.  Called from pollGstBus and only
// when it has been determined that stream is playing
void GST_Interface::queryStreamPosition()
{
	// return now if we ever failed getting a stream position
	if (! b_positionenabled) return;
	
	gint64 position = 0;	// the position in nanoseconds 
	
	if (!gst_element_query_position (pipeline, GST_FORMAT_TIME, &position)) {
		b_positionenabled = false;
		gst_element_post_message (pipeline,
			gst_message_new_application (GST_OBJECT (pipeline),
				gst_structure_new ("Application", "MBMP", G_TYPE_STRING, "Error: Could not query the stream position", NULL))); 
			 }	// if query failed
	else {
		qobject_cast<PlayerControl*>(mainwidget)->setPositionWidgets(position / (1000 * 1000 * 1000) );
	}	// else query succeeded       
	
	return;
}	

//
// function to query the stream to see if we can seek in it.  Called in 
// pollGstBus when the player state changes into a PLAYING state. First
// check to make sure there is a stream playing.  This should actually
// be checked before the function is called, but just in case.
bool GST_Interface::queryStreamSeek()
{
	// Make sure we are playing
	if (getState() != GST_STATE_PLAYING ) return false;
	
	// Variables
	GstQuery* query = 0;
	gint64 start = 0;	// for now we don't do anything with start and end
	gint64 end = 0;
	gboolean seek_enabled = false;
		
	// Query the stream see if we can seek in it
	query = gst_query_new_seeking (GST_FORMAT_TIME);
  if (gst_element_query (pipeline, query)) {
		gst_query_parse_seeking (query, NULL, &seek_enabled, &start, &end);
	}
	else {
		gst_element_post_message (pipeline,
			gst_message_new_application (GST_OBJECT (pipeline),
				gst_structure_new ("Application", "MBMP", G_TYPE_STRING, "Error: Could not determine if seek is possible - disabling seeking in the stream", NULL)));
	}
	
  gst_query_unref (query);
  return static_cast<bool>(seek_enabled);
 }
  
 //
 // Function to query the stream duration.  Return the duration in 
 // gstreamer standard nanoseconds.  Called from two locations in pollGstBus
 // one is in the DURATION case which is mainly for VBR streams, and we
 // only treate consider it for informational purposes.  The second is when
 // the STATE changes to PLAYING.  This is used to set the duration widgets.
 gint64 GST_Interface::queryDuration()
 {
	gint64 duration = 0;	// the duration in nanoseconds (nanoseconds!! are you kidding me)
	
	if (!gst_element_query_duration (pipeline, GST_FORMAT_TIME, &duration)) {
		gst_element_post_message (pipeline,
			gst_message_new_application (GST_OBJECT (pipeline),
				gst_structure_new ("Application", "MBMP", G_TYPE_STRING, "Error: Could not query the stream duration", NULL))); 
	 }	// if query failed
	 
	 return duration;
 } 
