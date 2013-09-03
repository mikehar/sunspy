//
//  sunspy.c
//
//  Copyright (c) 2013 mike harrington. All rights reserved.
//  Released to the public domain by Mike Harrington, September 2013
//

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>
#include <getopt.h>
#include <pwd.h>
#include <unistd.h>

#include "libconfig.h"
#include "sunspy.h"
#include "sunriset.h"

float version = 1.0;

// Basic Camera info
typedef struct camera_t {
    const char *name;       // securityspy text name
    unsigned number;        // securityspy camera number
    const char *str_start;  // unparsed start time i.e "sunrise+30"
    const char *str_stop;   // unparsed stop time
    time_t start;           // computed next start time
    time_t stop;            // computed next stop time
    struct camera_t *next;  // sll
} camera_t;

camera_t *cameralist = NULL;
int numcams = 0;

// Event info.
// At this time we only support two events, set the camera
// ACTIVE or PASSIVE
#define CAM_ACTION_ACTIVE 1
#define CAM_ACTION_PASSIVE 2
typedef struct camevent_t {
    unsigned action;        // active or passive
    unsigned camera;        // camera id
    time_t  starttime;      // computed execution time
    const char *str_time;   // unparsed execution time i.e. "sunrise+30"
    struct camevent_t *next; // sll
} camevent_t;

camevent_t *camevents = NULL;

//ttSunrise is today's sunrise. ttNextSunrise is the next
// sunrise that will occur. If we're past today's sunrise already,
// then ttNextSunrise will have tomorrow's sunrise time.
time_t ttSunrise, ttNextSunrise;
time_t ttSunset, ttNextSunset;
time_t ttNoon, ttNextNoon;

// Values pased in via the command line override the config file.
#define BOGUS   255
double lat = BOGUS;
double lon = BOGUS;
double tz = BOGUS;
char *url = NULL;                   // url of SecuritySpy server
char *user = NULL;                  // SecuritySpy user
char *password = NULL;              // SecuritySpy Password
bool verbose = true;               // commandline flag
bool noaction = false;              // commandline flag. Print out what would have happened.
char *camera_id = NULL;             // commandline flag. camera id for single camera
char *camera_start = NULL;          //commandline flag.
char *camera_stop = NULL;           //commandline flag.
char *configfile = NULL;            //commandline flag.
char *userip = NULL;                // detected user IP
bool forceaction = false;           // command line flag. Forces action to happen now, no sleeping.
char *defaultconfigpath = NULL;
bool askforpassword = false;        // if -p or --password is specificed without a password, ask

void usage()
{
    printf("sunspy version %1.1f\n", version);
    printf("\n");
    printf("sunspy [options]\n");
    printf("\n");
    printf(" sunspy is an application that allows your to remotely\n");
    printf(" set camera state on SecuritySpy based on sunries and sunset\n");
    printf(" times.\n");
    printf("\n");
    printf(" options can be specified via the command line or with a config\n");
    printf(" file.\n");
    printf("\n");
    printf(" -c         Specifies location of config file. You can also\n");
    printf(" --config    place sunspy.conf file in the same dir as the\n");
    printf("             sunspy binary and that will be loaded automatically.\n");
    printf("            Command line options override the config file.\n");
    printf(" --force\n");
    printf("\n");
    printf(" --verbose  Verbose.\n");
    printf(" -v\n");
    printf("\n");
    printf(" --noaction  Prints out first pass of all actions and exists.\n");
    printf(" -n\n");
    printf("\n");
    printf(" --cameraid  SecuritySpy Camera Id. Useful when just doing one\n");
    printf(" -i             camera from the command line.\n");
    printf("\n");
    printf(" --user      Security Spy User\n");
    printf(" -u\n");
    printf("\n");
    printf(" --password  Prompt for security spy password. Password can also\n");
    printf(" -p             be supplied in the config file.\n");
    printf("\n");
    printf(" --url       SecuritySpy url (i.e. \"http://192.168.1.5:8000\")\n");
    printf(" -w\n");
    printf("\n");
    printf(" --start    Time to start or stop (set camera to ACTIVE or PASSIVE).\n");
    printf(" --stop     Times are all sunrise or sunset relative. You can also specify\n");
    printf("            a hour or minute modifier. examples:\n");
    printf("                sunrise         Event happens at sunrise\n");
    printf("                sunset-30m      Event happens 30 minutes before sunset\n");
    printf("                noon+1h         Event happens 1 hour after noon\n");
    printf("                +8h             Event happens 8 hour after the start event,\n");
    printf("                                    only valid for stop events.\n");
    printf("\n");
    printf(" --lat      Lat/Lon. If not supplied, we will try to use freegeoip.net to\n");
    printf(" --lon          determine your lat/lon.\n");
    printf(" \n");
    printf(" --timezone If not supplied, we will automatically detect your timezone.\n");
    printf(" -t         Hours from GMT.\n");
    printf(" \n");
    exit(0);
}

// prettyHour
// converts a float to a human readable sting HH:MM
char *prettyHour(double t, char *dest)
{
    static char szbuf[20];
    if (dest == NULL) dest = szbuf;
    
    double di, df;
    di = floor(modf(t, &df) * 60);
    sprintf(dest, "%d:%02d", (int)df, (int)di);
    return dest;
}

// Takes the double hour value and replaces the hour and minute value in the provided time_t.
// Seconds are set to 0.
time_t convertTime(time_t day, double hour)
{
    struct tm tmDate = *localtime(&day);
    tmDate.tm_hour = floor(hour);
    tmDate.tm_min = floor((hour - tmDate.tm_hour) * 60);
    tmDate.tm_sec = 0;
    return mktime(&tmDate);
}

/*
 * Calculate the sunrise and sunset for this location
 */
void calctime(sunrise_t *sr, double lat, double lon, const char *twilight_type, double angle, time_t *tt)
{
    sr->latitude       = lat;
    sr->longitude      = lon;
    sr->twilightAngle  = TWILIGHT_ANGLE_DAYLIGHT;
    sr->riseTime       = 0.0;
    sr->setTime        = 0.0;
    
    // The sunset calculator requires the number of days since Jan 0, 2000
    struct tm targetday = *gmtime(tt);
    sr->daysSince2000 = daysSince2000 (targetday.tm_year + 1900, targetday.tm_mon + 1, targetday.tm_mday);
    
    if   (!strcmp (twilight_type, "daylight"))
        sr->twilightAngle = TWILIGHT_ANGLE_DAYLIGHT;
    else if   (!strcmp (twilight_type, "civil"))
        sr->twilightAngle = TWILIGHT_ANGLE_CIVIL;
    else if   (!strcmp (twilight_type, "nautical"))
        sr->twilightAngle = TWILIGHT_ANGLE_NAUTICAL;
    else if   (!strcmp (twilight_type, "astronomical"))
        sr->twilightAngle = TWILIGHT_ANGLE_ASTRONOMICAL;
    else if   (!strcmp(twilight_type, "angle"))
    {
        sr->twilightAngle = angle;
    }
    
    // Co-ordinates must be in 0 to 360 range
    sr->latitude  = revolution (sr->latitude);
    sr->longitude = revolution (sr->longitude);
    
    if (sr->twilightAngle <= -90 || sr->twilightAngle >= 90)
    {
        fprintf(stderr, "Error: Twilight angle must be between -90 and +90 (-ve = below horizon), your setting: %f\n", sr->twilightAngle);
        sr->twilightAngle = TWILIGHT_ANGLE_DAYLIGHT;
    }
    
    sunriset (sr);
}

/*
 * Initializes ttSunset, ttNoon, ttSunrise.
 *
 * If the current time is greater than any of the events, the time
 * is calculated for tomorrows event. If it's after sunrise when this
 * executes, then it calcuates the sunrise for tomorrow, the past is
 * of no use to us.
 */
void calc_sunrise_sunset(time_t tt)
{
    // set date for sunrise/sunset prediction
    struct tm tmLocal = *localtime (&tt);
    sunrise_t sr;
    
    // Calc sunrise/sunset for today
    calctime(&sr, lat, lon, "civil", 0, &tt);
    double riseToday = sr.riseTime + tz;
    double noonToday = sr.noonTime + tz;
    double setToday = sr.setTime + tz;
    

    ttSunrise = convertTime(tt, riseToday);
    ttNextSunrise = ttSunrise;

    ttNoon = convertTime(tt, noonToday);
    ttNextNoon = ttNoon;
  
    ttSunset = convertTime(tt, setToday);
    ttNextSunset = ttSunset;
  
    // Calc sunrise/sunset for tomorrow
    time_t ttTomorrow = tt + (60*60*24); // + 24hrs to now in seconds
    
    calctime(&sr, lat, lon, "civil", 0, &ttTomorrow);
    double riseTomrrow = sr.riseTime + tz;
    double noonTomorrow = sr.noonTime + tz;
    double setTomorrow = sr.setTime + tz;
    
    if (verbose)
    {
        char srise[10], snoon[10], sset[10];
        printf ("Today \t\tsunrise: %s\tnoon: %s\tsunset: %s\n", prettyHour(riseToday, srise), prettyHour(noonToday, snoon), prettyHour(setToday, sset));
        printf ("Tomorrow \tsunrise: %s\tnoon: %s\tsunset: %s\n", prettyHour(riseTomrrow, srise), prettyHour(noonTomorrow, snoon), prettyHour(setTomorrow, sset));
    }
    
    // If we are already past the events, pick up the time for tomorrow
    double currentTime = tmLocal.tm_hour + tmLocal.tm_min/60.0 + tmLocal.tm_sec/3600;
    if (riseToday <= currentTime)
        ttNextSunrise = convertTime(ttTomorrow, riseTomrrow);
    if (noonToday <= currentTime)
        ttNextNoon = convertTime(ttTomorrow, noonTomorrow);
    if (setToday <= currentTime)
        ttNextSunset = convertTime(ttTomorrow, setTomorrow);
 }

//
// Converts from [sunrise|sunset][+|-][number][h|m] to an actual time.
//
time_t decodetime(const char *timestr)
{
    const char *org = timestr;
    time_t result = 0;
    int mod = 1;         // default to positive
    int diffseconds = 0; // number of seconds to modify the time with
    
    while (*timestr)
    {
        if (*timestr == '+')
        {
            timestr++;
        } else if (*timestr == '-') {
            mod *= -1;
            timestr++;
        } else if (isdigit(*timestr)) {
            int  i = 0;
            char numstr[10];
            while (i < 9 && isdigit(*timestr))
            {
                numstr[i++] = *timestr++;
            }
            numstr[i] = 0;
            mod *= atoi(numstr);
            
        } else if (!strncasecmp("sunrise", timestr, 7)) {
            result = ttNextSunrise;
            timestr += 7;
        } else if (!strncasecmp("noon", timestr, 4)) {
            result = ttNextNoon;
            timestr += 4;
        } else if (!strncasecmp("sunset", timestr, 6)) {
            result = ttNextSunset;
            timestr += 6;
        } else if (!strcasecmp("h", timestr)) {
            diffseconds = 60*60; //seconds in an hour;
            break; // this has to be last.
        } else if (!strcasecmp("m", timestr)) {
            diffseconds = 60; //seconds in a minute;
            break;
        } else {
            fprintf(stderr, "Unknown time '%s'\n", org);
            break;
        }
        
    }
    
    // Allow for a time of "+5h" which we interpert to be "sunrise+5h".
    if (result == 0)
    {
        // Make sure we haven't passed this event
        // This needs to be refactored to live elsewhere.
        if ((ttSunrise + (mod * diffseconds)) > time(NULL))
            result = ttSunrise;
        else
            result = ttNextSunrise;
    }
    
    result += mod * diffseconds;
    return result;
}

//
// place holder fuction to keep curl from writing to stdout
//
size_t curlwritebogus(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    return size*nmemb;
}
//
// Call SecuritySpy webapi
//
int httpcmd(const char *url, const char *user, const char *password)
{
    CURL *crl = curl_easy_init();
    curl_easy_setopt(crl, CURLOPT_URL, url);
    
    if (user && password) {
        char *userpass = malloc(strlen(user)+ strlen(password) + 2);
        sprintf (userpass, "%s:%s", user,password);
        curl_easy_setopt (crl, CURLOPT_USERPWD, userpass);
        free (userpass);
    }
    curl_easy_setopt(crl, CURLOPT_WRITEFUNCTION, curlwritebogus);
    int iret = curl_easy_perform(crl);
    if (iret) {
        fprintf(stderr, "curl failed. [%d]\n", iret);
        return false;
    }
    
    int httpcode = 0;
    curl_easy_getinfo(crl, CURLINFO_RESPONSE_CODE, &httpcode);
    curl_easy_cleanup(crl);
    
    return httpcode;
}

//
// Check to see if we can connet to the server
//
bool isconnected()
{
    if (noaction)
        return true;
    
    char strip[1000]; // yeah, i know.
    // build command string for ss web api
    sprintf(strip, "%s/++systemInfo", url);
    
    if (verbose)
        printf("Checking connection. %s @ %s\n", user, url);
    
    int httpcode = httpcmd(strip, user, password);
    
    if (httpcode != 200)
        printf("Failed to connect to server. %d\n", httpcode);
    else if (verbose)
        printf("Passed.\n");

    return httpcode == 200;
}

//
// Add camera to our array
//
void addcamera(const char *name, unsigned number, const char *start, const char *stop)
{
    camera_t *new = malloc(sizeof(camera_t));
    new->name = name;
    new->number = number;
    new->str_start = start;
    new->str_stop  = stop;
    new->next = NULL;

    if (cameralist)
        new->next = cameralist;

    cameralist = new;
}


//
// Insert new event into list sorted by time
//
void addevent(camevent_t *event)
{
    event->next = NULL;
    if (!camevents)
    {
        camevents = event;
    }
    else
    {
        camevent_t *t = camevents, *tp = NULL;
        while (t != NULL && event->starttime > t->starttime)
        {
            tp = t;
            t = t->next;
        }

        if (tp)
        {
            event->next = tp->next;
            tp->next = event;
        }
        else
        {
            event->next = camevents;
            camevents = event;
        }
    }
}

//
// removes event, does not free.
//
void removeevent(camevent_t *event)
{
    camevent_t *t = camevents, *tp = NULL;
    while (t != event)
    {
        tp = t;
        t = t->next;
    }
    if (tp == NULL)
        camevents = event->next;
    else
        tp->next = event->next;
    event->next = NULL;
}

//
// This is the daemon loop, it never returns.
//
void camloop()
{
    time_t tt = time (NULL);
    
    while (camevents) {
        camevent_t *e = camevents;
        
        // let time magically advance in noaction mode.
        if (!noaction)
            tt = time (NULL); // the time is now
        
        // wait for next event
        if (e->starttime > tt && !noaction && !forceaction)
        {
            //if (verbose)
                printf("Sleeping until %s, %s", e->str_time, ctime(&e->starttime));
            sleep((unsigned)(e->starttime - tt));
            if (verbose)
            {
                time_t tn = time(NULL);
                printf("Woke up at %s.", ctime(&tn));
            }
        }
        if (noaction|verbose)
        {
            printf("Event %s scheduled for %s", e->str_time, ctime(&e->starttime));
            tt = e->starttime + (60*60);
        }
        
        char strip[1000]; // yeah, i know.
        // build command string for ss web api
        if (e->action == CAM_ACTION_ACTIVE)
            sprintf(strip, "%s/++ssControlActiveMode?cameraNum=%d", url, e->camera);
        else //CAM_ACTION_PASSIVE
            sprintf(strip, "%s/++ssControlPassiveMode?cameraNum=%d", url, e->camera);

        // call the web server
        if (noaction||verbose)
            printf("%s @ %s\n", user, strip);

        if(!noaction)
        {
            int httpcode = httpcmd(strip, user, password);
            if (httpcode != 200)
                fprintf(stderr, "Warning: Server returned %d\n", httpcode);
        }
        
        // reschedule
        removeevent(e);
        
        // noaction and forceaction only execute each event once.
        if (!noaction && !forceaction)
        {
            // recalc times
            calc_sunrise_sunset(tt);
            e->starttime = decodetime(e->str_time);
            
            // add event back to the queue
            addevent(e);
        }
    }
}

//
// parses the command line and sets up the globals
//
void parsecl(int argc, const char *argv[])
{
    int c;
    while (true)
    {
        static struct option long_options[] =
        {
            {"config", required_argument, NULL, 'c'},
            {"force", no_argument, &forceaction, true},
            {"verbose", no_argument, &verbose, true},
            {"noaction", no_argument, &noaction, true},
            {"cameraid", required_argument, NULL, 'i'},
            {"action", required_argument, NULL, 'a'},
            {"user", required_argument, NULL, 'u'},
            {"password", no_argument, NULL, 'p'},
            {"url", required_argument, NULL, 'w'},
            {"start", required_argument, NULL, 'j'},
            {"stop", required_argument, NULL, 'k'},
            {"lat", required_argument, NULL, 'l'},
            {"lon", required_argument, NULL, 'm'},
            {"timezone", required_argument, NULL, 't'},
            {"help", no_argument, NULL, '?'},
            {0,0,0,0}
        };

        int option_index = 0;
        c = getopt_long(argc, (char * const *)argv, "?i:t:a:c:u:pw:vn", long_options, &option_index);
        
        if (c == -1) break;
        
        switch (c) {
            case 0:
                break;

            case 'c':
                configfile = malloc(strlen(optarg)+1);
                strcpy(configfile, optarg);
                break;
            case 'i':
                camera_id = malloc(strlen(optarg)+1);
                strcpy(camera_id, optarg);
                break;
            case 'a':
                printf("--action or -a is not supported yet.\n");
                /*

                if (!strncasecmp("active", optarg, 6))
                    action = CAM_ACTION_ACTIVE;
                else if (!strncasecmp("passive", optarg, 6))
                    action = CAM_ACTION_PASSIVE;
                else
                {
                    fprintf(stderr, "Unknown --action paramter [%s]. Must be 'active' or 'passive'.\n", optarg);
                    exit(-1);
                }*/
                break;
            case 'u':
                user = malloc(strlen(optarg)+1);
                strcpy(user, optarg);
                break;
            case 'p':
                password = NULL;
                askforpassword = true;
                break;
            case 'w':
                url = malloc(strlen(optarg)+1);
                strcpy(url, optarg);
                break;
            case 'j':
                camera_start = malloc(strlen(optarg)+1);
                strcpy(camera_start, optarg);
                break;
            case 'k':
                camera_stop = malloc(strlen(optarg)+1);
                strcpy(camera_stop, optarg);
                break;
            case 'l':
                lat = strtod(optarg, NULL);
                break;
            case 'm':
                lon = strtod(optarg, NULL);
                break;
            case 't':
                tz = strtod(optarg, NULL);
                break;
            case 'n':
                noaction = true;
                break;
            case 'v':
                verbose = true;
                break;
            case '?':
                usage();
                break;
            default:
                break;
        }
    }
}

//
// reads the config file and sets up the globals.
//
bool readconfig()
{
    if (configfile == NULL) {
        configfile = defaultconfigpath;
    }
    
    config_t cfg;
    config_init(&cfg);
    if (config_read_file(&cfg, configfile) == CONFIG_FALSE) {
        config_destroy(&cfg);
        if (verbose)
            printf("Not using a config file.\n");
        return false;
     }
    
    if (verbose)
        printf("Using config file:%s\n", configfile);
    
    if (!url)
        if(!config_lookup_string(&cfg, "server_address", (const char **)&url))
            fprintf(stderr, "No 'server_address' setting in configuration file.\n");
    if (!user)
        if(!config_lookup_string(&cfg, "user", (const char **)&user))
            fprintf(stderr, "No 'user' setting in configuration file.\n");

    if (!url || !user)
    {
        fprintf(stderr, "Missing url, user.\n");
        exit(-1);
    }
    
    if (lat == BOGUS && lon == BOGUS)
    {
        const char *latstr = NULL, *lonstr = NULL;
        config_lookup_string(&cfg, "lat", &latstr);
        config_lookup_string(&cfg, "lon", &lonstr);
        if (latstr && lonstr)
        {
            lat = strtod(latstr, NULL);
            lon = strtod(lonstr, NULL);
        }
    }
    
    if (tz == BOGUS)
    {
        const char *timezonestr;
        if (config_lookup_string(&cfg, "timezone", &timezonestr))
        {
            tz = strtod(timezonestr, NULL);
        }
    }
    
    // Did we get a camera from the command line?
    if (camera_id && camera_start && camera_stop)
    {
        addcamera("commandline", atoi(camera_id), camera_start, camera_stop);
        
    } else {
        // Get the camera list
        config_setting_t *cameras = config_lookup(&cfg, "cameras");
        if (!cameras)
        {
            fprintf(stderr, "No Cameras in config file!");
            exit(-1);
        }
    
        int count = config_setting_length(cameras);
        for (int i = 0; i < count; i++)
        {
            config_setting_t *camera = config_setting_get_elem(cameras, i);
            const char *name, *start, *stop;
            int id;
            
            if (!(config_setting_lookup_int(camera, "number", &id)
                  && config_setting_lookup_string(camera, "name", &name)
                  && config_setting_lookup_string(camera, "start", &start)
                  && config_setting_lookup_string(camera, "stop", &stop)))
            {
                fprintf(stderr, "Invalid Camera #%d\n", i);
            } else {
                addcamera(name, (unsigned)id, start, stop);
            }
        }
    }
    return true;
}


int curlbufsize = 1024;
int curlbufwritten;
char *curlbuf;

// helper function for fetchLatLon. Not robust.
size_t curlwrite(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t i = size * nmemb;
    if (curlbufwritten+i > curlbufsize-1)
    {
        fprintf(stderr, "Exceeding curl buffer.\n");
        return 0;
    }
    
    memcpy(&curlbuf[curlbufwritten], ptr, i);
    return i;
}

//
// Fetch Lat Long from fregeoiop.net.
//
void fetchLatLon()
{
    CURL *crl = curl_easy_init();

    curlbufwritten = 0;
    curlbuf = malloc(curlbufsize);
    
    const char *freegeoip = "http://freegeoip.net/csv";
    curl_easy_setopt(crl, CURLOPT_URL, freegeoip);
    curl_easy_setopt(crl, CURLOPT_WRITEFUNCTION, curlwrite);
    int iret = curl_easy_perform(crl);
    if (iret && verbose) {
        fprintf(stderr, "curl failed. %d", iret);
    }
    
    int httpcode = 0;
    curl_easy_getinfo(crl, CURLINFO_RESPONSE_CODE, &httpcode);
    curl_easy_cleanup(crl);
    
    
    // Parse response
    // sample : "64.119.5.150","US","United States","WA","Washington","Friday Harbor","98250","48.5372","-123.0679","819","360"
    // Soooper hacky.
    if (httpcode == 200)
    {
        curlbuf[curlbufwritten] = 0; // just in case
        
        // Get the IP
        char *struserip = strtok(&curlbuf[1], "\",\"");// skip initial '"'.
        userip = malloc(strlen(struserip)+1);
        strcpy(userip, struserip);
        
        // skip the next six fields
        for (int i = 0; i < 6; i++) strtok(NULL, "\",\"");
        
        // get the lat lon
        char *strlat = strtok(NULL, "\",\"");
        char *strlon = strtok(NULL, "\",\"");
        lat = strtod(strlat, NULL);
        lon = strtod(strlon, NULL);
        
        if (verbose||noaction)
            printf ("lat/lon detected as %f, %f\n", lat, lon);
    }
   
}

//
// Retrieve the Timezone for the local machine
//
void fetchTZ()
{
    time_t tt = time (NULL);
    struct tm tmLocal = *localtime (&tt);
    tz = tmLocal.tm_gmtoff/(60.0*60.0); //convert from seconds to factional hours
    if (verbose||noaction)
        printf ("Timezone detected as %s %s\n", prettyHour(tz, NULL), tmLocal.tm_zone);
}

//
//The big kahuna.
//
int main(int argc, const char * argv[])
{
    defaultconfigpath = malloc(strlen(argv[0])+strlen(".conf"));
    sprintf(defaultconfigpath, "%s.conf", argv[0]);
 
    // parse command line args
    parsecl(argc, argv);

    if (verbose)
        printf("sunspy version %1.1f\n", version);
    
    // parse config file
    // if no config file, we need at least a few args
    // site, camera, start, user
    if (!readconfig() && argc < 5)
        usage();
    
    // Try to fill in lat/lon and timezone if not provided.
    if (lat == BOGUS || lon == BOGUS)
        fetchLatLon();
    
    // Still no lat/lon? Pft.
    if (lat == BOGUS || lon == BOGUS)
    {
        fprintf(stderr, "Missing lat/lon. Use --lat and --lon or put them in the config file.\n");
        exit(-1);
    }
    
    // Try to fill in the timezone if not provide, otherwise it defaults to a useless
    //   gmt 0 time.
    if (tz == BOGUS)
        fetchTZ();
    
    // Still no timezone? Bail.
    if (tz == BOGUS)
    {
        fprintf(stderr, "Missing timezone. Use the -t or --timezone option or put it in the config files\n");
        exit(-1);
    }
    
    if (askforpassword)
    {
        password  = malloc(_PASSWORD_LEN+1);
        strcpy(password, getpass("password:"));
    } else if (!password) {
        if (verbose)
            printf("warning: no password was given.\n");
    }
 
    if (!isconnected())
    {
        exit(-1);
    }
    
    // initial sunrise/sunset calculation
    calc_sunrise_sunset(time(NULL));
    
    if (verbose)
    printf("Events:\n");
    
    // initial setup for cameras
    for (camera_t *cam = cameralist; cam; cam = cam->next)
    {
        // Add start time
        camevent_t *e = (camevent_t *)malloc(sizeof(camevent_t));
        e->action = CAM_ACTION_ACTIVE;
        e->camera = cam->number;
        e->starttime = decodetime(cam->str_start);
        e->str_time = cam->str_start;
        addevent(e);
  
        if (verbose)
            printf("Set camera #%d to ACTIVE at %s", cam->number, ctime(&e->starttime));

        
        // Add stop time
        e = (camevent_t *)malloc(sizeof(camevent_t));
        e->action = CAM_ACTION_PASSIVE;
        e->camera = cam->number;
        e->starttime = decodetime(cam->str_stop);
        e->str_time = cam->str_stop;
        addevent(e);
        
        if (verbose)
            printf("Set camera #%d to PASSIVE at %s", cam->number, ctime(&e->starttime));
   }

    if (verbose)
        printf("\n");
    
    // daemon loop
    camloop();
    
    printf("done.\n");
    return 0;
}


