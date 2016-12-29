/***************************** mpris2.cpp *****************************

Code for the MPRISv2.2 interface on DBus.  When this object is registered 
MBMP will communicate to other processes.  

Copyright (C) 2013-2016
by: Andrew J. Bibb
License: MIT 

Permission is hereby granted, free of charge, to any person obtaining a copy 
of this software and associated documentation files (the "Software"),to deal 
in the Software without restriction, including without limitation the rights 
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
copies of the Software, and to permit persons to whom the Software is 
furnished to do so, subject to the following conditions: 

The above copyright notice and this permission notice shall be included 
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
DEALINGS IN THE SOFTWARE.
***********************************************************************/  


# ifndef IPC_MPRIS2
# define IPC_MPRIS2

# include <QObject>

# define IPC_SERVICE "org.mpris.MediaPlayer2.mbmp"
# define IPC_OBJECT "/org/mpris/MediaPlayer2"
# define IPC_INTERFACE_MEDIAPLAYER2 "org.mpris.MediaPlayer2"
# define IPC_INTERFACE_MEDIAPLAYER2PLAYER "org.mpris.MediaPlayer2.Player"

class Mpris2 : public QObject
{
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", IPC_INTERFACE_MEDIAPLAYER2)
    
  public:
    Mpris2 (QObject* parent = 0);
};		

#endif
