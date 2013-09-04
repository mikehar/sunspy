sunspy
======

Unofficial SecuritySpy utility to manage camera status based on sunrise/sunset times


Binary's are in the release diretory.

To build, you need to install utilconfig. I recommend using installing utilconfig without dynamic libraries.

  ./configure --disable-shared
  make clean
  make
  make install
  
Useage:
sunspy version 1.0

sunspy [options]

 sunspy is an application that allows your to remotely
 set camera state on SecuritySpy based on sunries and sunset
 times.

 options can be specified via the command line or with a config
 file.

 -c         Specifies location of config file. You can also
 --config    place sunspy.conf file in the same dir as the
             sunspy binary and that will be loaded automatically.
            Command line options override the config file.
 --force

 --verbose  Verbose.
 -v

 --noaction  Prints out first pass of all actions and exists.
 -n

 --cameraid  SecuritySpy Camera Id. Useful when just doing one
 -i             camera from the command line.

 --user      Security Spy User
 -u

 --password  Prompt for security spy password. Password can also
 -p             be supplied in the config file.

 --url       SecuritySpy url (i.e. "http://192.168.1.5:8000")
 -w

 --start    Time to start or stop (set camera to ACTIVE or PASSIVE).
 --stop     Times are all sunrise or sunset relative. You can also specify
            a hour or minute modifier. examples:
                sunrise         Event happens at sunrise
                sunset-30m      Event happens 30 minutes before sunset
                noon+1h         Event happens 1 hour after noon
                +8h             Event happens 8 hour after the start event,
                                    only valid for stop events.

 --lat      Lat/Lon. If not supplied, we will try to use freegeoip.net to
 --lon          determine your lat/lon.
 
 --timezone If not supplied, we will automatically detect your timezone.
 -t         Hours from GMT.
