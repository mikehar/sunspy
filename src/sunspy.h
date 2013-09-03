
#ifndef LAUNCHSS_H
  #define LAUNCHSS_H

typedef int bool;
#define true 1
#define false 0

#define NOT_SET 9999

typedef enum
{ DAYTYPE_NORMAL      = 0
, DAYTYPE_POLAR_DAY   = 1 // AKA midnight sun
, DAYTYPE_POLAR_NIGHT = 2 
} DayType;

typedef struct
{ 
  double latitude;            // Degrees -S/N
  double longitude;           // Degrees E/-W
  unsigned int daysSince2000;  
  double twilightAngle;    // Degrees, -ve = below horizon
    
  double riseTime;         // Sunrise    - time of, Unit: hours, GMT
  double noonTime;         // Solar noon - time of, Unit: hours, GMT
  double setTime;          // Sunset     - time of, Unit: hours, GMT
  DayType  dayType;        // Normal, Polar Day, Polar Night
} sunrise_t;

#endif



