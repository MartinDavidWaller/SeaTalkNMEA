/*
 * SeaTalkNMEA
 * 
 * This uses the Arduino Leonardo.
 * 
 * April 10th, 2018 M.D.Waller
 * a) Bug fix. It was not processing the month correctly from a SeaTalk Date sentence. It
 *    was using the lower nibble and not the upper nibble shifted down.
 * b) Bug fix. It was not processing the minutes correctly from the SeaTalk Data sentence.
 *    It was not processing RS!
 *    
 * April 23rd, 2018 M.D.Waller
 * a) Bug fix. I was converting the wind speed to knots when it was already in knots! This
 *    exhibited as the wind speed being a factor of 2 out!
 *
 * May 7th, 2020 M.D.Waller
 * a) Bux fix. Jan Dytrych reported that the depthBelowTransducer calculate was wrong. 
 * It should be ((b[4] * 256 + b[3]) / 10) * FEET_TO_METERS, and not ((b[3] * 256 + b[4])
 * / 10) * FEET_TO_METERS
 */

//#define DEBUG 1
 
#include <HardwareSerial.h>
//include <SoftwareSerial9.h>
#include <EEPROM.h>

// https://github.com/datafl4sh/seatalk

#define HS_BAUD_RATE 4800

#define PRODUCT_NAME "SeaTalkNMEA"
#define MAJOR_VERSION 0
#define MINOR_VERSION 3

// Commands

#define COMMAND_DEBUG 'D'
#define COMMAND_HELP_1 'H'
#define COMMAND_HELP_2 '?'
#define COMMAND_TALKERID 'T'

#define COMMAND_TRUE 'T'
#define COMMAND_FALSE 'F'

// EEPROM Memory Layout

// EEPROM Version Information

#define EEPROM_MAJOR_VERSION 0
#define EEPROM_MINOR_VERSION (EEPROM_MAJOR_VERSION + 1)

// EEPROM Talker ID Information

#define EEPROM_TALKERID_1 (EEPROM_MINOR_VERSION + 1)
#define EEPROM_TALKERID_2 (EEPROM_TALKERID_1 + 1)

// EEPROM Debug Information

#define EEPROM_DEBUG_SEATALK (EEPROM_TALKERID_2 + 1)

// NMEA Manifests

#define NMEA_DEFAULT_TALKERID_1 'G'
#define NMEA_DEFAULT_TALKERID_2 'P'

#define NMEA_PROPRIETARY "$PIMSST1,"
#define NMEA_DATA "D,"
#define NMEA_UNHANDLED "U,"
#define NMEA_VERSION "V"
#define NMEA_COMMA ","
#define NMEA_STAR "*"
#define NMEA_ZERO "0"

#define METRES_TO_FEET 3.28084
#define METRES_TO_FATHONS 0.546807
#define FEET_TO_METERS 0.3048

#define NMEA_RELATIVE "R"
#define NMEA_STATUS_A "A"
#define NMEA_METRES "M"
#define NMEA_FEET "f"
#define NMEA_FATHONS "F"

#define NMEA_DBT "DBT"
#define NMEA_DPT "DPT"
#define NMEA_GGA "GGA"
#define NMEA_GLL "GLL"
#define NMEA_MWV "MWV"
#define NMEA_RMC "RMC"
#define NMEA_VHW "VHW"
#define NMEA_VTG "VTG"
#define NMEA_ZDA "ZDA"

// SeaTalk Manifests

#define SEATALK_DEPTH_BELOW_TRANSDUCER 0x00
#define SEATALK_APPARENT_WIND_ANGLE 0x10
#define SEATALK_APPARENT_WIND_SPEED 0x11
#define SEATALK_SPEED_THROUGH_WATER 0x20
#define SEATALK_LATITUDE 0x50
#define SEATALK_LONGITUDE 0x51
#define SEATALK_SPEED 0x52
#define SEATALK_COG 0x53
#define SEATALK_TIME 0x54
#define SEATALK_DATE 0x56
#define SEATALK_SATINFO 0x57                  // Not handled yet!
#define SEATALK_LATLON 0x58
#define SEATALK_TARGET_WAYPOINT_NAME 0x82
#define SEATALK_NAVIGATION_TO_WAYPOINT 0x85   // Not handled yet!
#define SEATALK_COMPASS_VARIATION 0x99        // Not handled yet!
#define SEATALK_UNKNOWN_001 0x9f              // Not handled yet!
#define SEATALK_DESTINATION_WAYPOINT  0xa1    // Not handled yet! 
#define SEATALK_GPS_DGPS_INFO 0xa5            // Not handled yet!
  
uint16_t b[255];
int bi = 0;
bool processingPacket = false;
int packetLength = 0;
char nmeaBuffer[255];

// Command buffer manifests and data

#define COMMAND_BUFFER_LENGTH 50

int commandBufferIndex;
char commandBuffer[COMMAND_BUFFER_LENGTH];

// NMEA Generation data

char talkerID[4] = {'$', '?', '?', '\0'};

// SeaTalk data

double depthBelowTransducer = -1;
double apparentWindAngle = -1;
double apparentWindSpeed = -1;
double latitude = -1;
double longitude = -1;
char *ew = NULL;
char *ns = NULL;
int day = -1;
int month = -1;
int year = -1;
int hours = -1;
int minutes = -1;
int seconds = -1;
double _speed = -1;
double cog = -1;
double speedThroughWater = -1;
boolean debugSeatalk = true;
int numberOfSatellites = -1;
double horizontalDillutionOfPosition = -1;

void startNMEAProprietary() {

  // Clear the buffer

  nmeaBuffer[0] = '\0';

  // Start off the sentence

  strcat(&nmeaBuffer[0],NMEA_PROPRIETARY);
}

void addNMEACheckSum()
{
  // Now we need to work out the checksum

  int checkSum = 0;
  for(int i = 1; i < strlen(nmeaBuffer); i++)
  {
    checkSum ^= nmeaBuffer[i];
  }

  // Now we can add the '*'

  strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_STAR);

  // Now add the checksum

  if (checkSum < 16)
    strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_ZERO);
  itoa(checkSum,&nmeaBuffer[strlen(nmeaBuffer)],16);
}

void addNMEAInteger(boolean withComma, int v)
{
    // Put in a leading comma

    if (true == withComma)  
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA);
      
    // Put in the integer
  
    itoa(v,&nmeaBuffer[strlen(nmeaBuffer)],10);
}

void addNMEADouble(boolean withComma, const double v,const int width,const int precision)
{
  // Put in a leading comma

  if (true == withComma)  
    strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA);

  // Add the decimal value
  
  dtostrf(v,width,precision,&nmeaBuffer[strlen(nmeaBuffer)]);
}

void addNMEAZeroTerminatedString(boolean withComma, const char *v)
{
  // Put in a leading comma

  if (true == withComma)
    strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA);

  // Add the string
  
  strcat(&nmeaBuffer[strlen(nmeaBuffer)],v); 
}

void formProprietaryNMEA()
{
  // Start the new sentence
  
  startNMEAProprietary();

  // Add the Data indicator

  strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_DATA);

  // Put in the packet data
  
  for(int i = 0; i < packetLength; i++)
  {
    itoa(b[i],&nmeaBuffer[strlen(nmeaBuffer)],16);
    if (i < packetLength - 1)
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA);
  }

  // Add in the checksum
  
  addNMEACheckSum();
}

void formUnhandledProprietaryNMEA()
{
  // Start the new sentence
  
  startNMEAProprietary();

  // Add the Data indicator

  strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_UNHANDLED);

  // Put in the packet data

  for(int i = 0; i < packetLength; i++)
  {
    itoa(b[i],&nmeaBuffer[strlen(nmeaBuffer)],16);
    if (i < packetLength - 1)
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA);
  }

  // Add in the checksum
  
  addNMEACheckSum();

  // Send it out

  Serial.println(nmeaBuffer); 
}

void sendNMEAVersionSentence()
{
  // Start the new sentence
  
  startNMEAProprietary();

  // Add the Version indicator

  strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_VERSION);

  // Add the product name

  addNMEAZeroTerminatedString(true,PRODUCT_NAME);

  // Put in the major version

  addNMEAInteger(true,MAJOR_VERSION);
  
  // Put in the minor version

  addNMEAInteger(true,MINOR_VERSION);

  // Add in the checksum
  
  addNMEACheckSum();

  // Send it out

  Serial.println(nmeaBuffer); 
}

void addNMEADate(boolean withComma)
{
  // Put in a leading comma

  if (true == withComma)
    strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA);

  // If we have a valid date
  
  if ((-1 != day) && (-1 != month) && (-1 != year))
  {
    if (day < 10)
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],"0");
    itoa(day,&nmeaBuffer[strlen(nmeaBuffer)],10);

    if (month < 10)
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],"0");
    itoa(month,&nmeaBuffer[strlen(nmeaBuffer)],10);      

    if (year < 10)
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],"0");            
    itoa(year,&nmeaBuffer[strlen(nmeaBuffer)],10);      
  }
}

void addNMEATime(boolean withComma,boolean withMilliseconds)
{
  // Put in a leading comma

  if (true == withComma)
    strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA);

  // If we have a valid time
  
  if ((-1 != hours) && (-1 != minutes) && (-1 != seconds))
  {
    if (hours < 10)
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],"0");
    itoa(hours,&nmeaBuffer[strlen(nmeaBuffer)],10);

    if (minutes < 10)
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],"0");
    itoa(minutes,&nmeaBuffer[strlen(nmeaBuffer)],10);      

    if (seconds < 10)
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],"0");            
    itoa(seconds,&nmeaBuffer[strlen(nmeaBuffer)],10);      

    if (true == withMilliseconds)
      addNMEAZeroTerminatedString(false,".000");
  }
}

void addNMEALatitudeLongitude(boolean withComma)
{
  // Put in a leading comma

  if (true == withComma)
    strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA);  

  // Check to see if we have values, if we don't then we'll add empty
  // fields.
  
  if ((NULL != ns) && (NULL != ew))
  {
    // Now add the Latitude and the NS indicator

    addNMEADouble(false,latitude,3,2);
    addNMEAZeroTerminatedString(true,ns);

    // Now add the Longitude and the EW indicator

    addNMEADouble(true,longitude,3,2);
    addNMEAZeroTerminatedString(true,ew);
  }
  else
  {
    // Comma for NW
    
    strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA); 

    // Comma for longitude and EW
    
    strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA); 
    strcat(&nmeaBuffer[strlen(nmeaBuffer)],NMEA_COMMA); 
  }
}

void generateDBT()
{
  //        1   2 3   4 5   6 7
  //        |   | |   | |   | |
  // $--DBT,x.x,f,x.x,M,x.x,F*hh<CR><LF>
  //
  //  Field Number:
  //  1) Depth, feet
  //  2) f = feet
  //  3) Depth, meters
  //  4) M = meters
  //  5) Depth, Fathoms
  //  6) F = Fathoms
  //  7) Checksum
      
  // Check if we have all the data necessary to proceed
  
  if (-1 != depthBelowTransducer)
  {
    // Yes we do!

    // Clear down the buffer and add the talker ID
    
    nmeaBuffer[0] = '\0';
    strcat(&nmeaBuffer[0],talkerID);

    // Now add the DBT

    addNMEAZeroTerminatedString(false,NMEA_DBT);

    //  1) Depth, feet

    addNMEADouble(true,depthBelowTransducer * METRES_TO_FEET,1,1);
     
    //  2) f = feet

    addNMEAZeroTerminatedString(true,NMEA_FEET); 
     
    //  3) Depth, meters

    addNMEADouble(true,depthBelowTransducer,1,1);
    
    //  4) M = meters

    addNMEAZeroTerminatedString(true,NMEA_METRES); 
    
    //  5) Depth, Fathoms

    addNMEADouble(true,depthBelowTransducer * METRES_TO_FATHONS,1,1);
    
    //  6) F = Fathoms

    addNMEAZeroTerminatedString(true,NMEA_FATHONS); 
    
    //  7) Checksum
  
    addNMEACheckSum();

    // Send it out

    Serial.println(nmeaBuffer); 
  }
}

void generateDPT()
{
  //        1   2   3
  //        |   |   |
  // $--DPT,x.x,x.x*hh<CR><LF>
  //
  //  Field Number:
  //  1) Depth, meters
  //  2) Offset from transducer, positive means distance from tansducer to water line negative means distance from transducer to keel
  //  3) Checksum
    
  // Check if we have all the data necessary to proceed
  
  if (-1 != depthBelowTransducer)
  {
    // Yes we do!

    // Clear down the buffer and add the talker ID
    
    nmeaBuffer[0] = '\0';
    strcat(&nmeaBuffer[0],talkerID);

    // Now add the DPT

    addNMEAZeroTerminatedString(false,NMEA_DPT);

    //  1) Depth, meters

    addNMEADouble(true,depthBelowTransducer,1,1);
    
    //  2) Offset from transducer, positive means distance from tansducer to water line negative means distance from transducer to keel

    addNMEAInteger(true,0);
    
    //  3) Checksum
  
    addNMEACheckSum();

    // Send it out

    Serial.println(nmeaBuffer); 
  }
}

void generateGGA()
{
  //        1         2       3 4        5 6 7  8   9  10 |  12 13  14   15
  //        |         |       | |        | | |  |   |   | |   | |   |    |
  // $--GGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh<CR><LF>
  //
  //  Field Number:
  //  1) Universal Time Coordinated (UTC)
  //  2) Latitude
  //  3) N or S (North or South)
  //  4) Longitude
  //  5) E or W (East or West)
  //  6) GPS Quality Indicator,
  //      0 - fix not available,
  //      1 - GPS fix,
  //      2 - Differential GPS fix (values above 2 are 2.3 features)
  //      3 = PPS fix
  //      4 = Real Time Kinematic
  //      5 = Float RTK
  //      6 = estimated (dead reckoning)
  //      7 = Manual input mode
  //      8 = Simulation mode
  //  7) Number of satellites in view, 00 - 12
  //  8) Horizontal Dilution of precision (meters)
  //  9) Antenna Altitude above/below mean-sea-level (geoid) (in meters)
  //  10) Units of antenna altitude, meters
  //  11) Geoidal separation, the difference between the WGS-84 earth ellipsoid and mean-sea-level (geoid), "-" means mean-sea-level below ellipsoid
  //  12) Units of geoidal separation, meters
  //  13) Age of differential GPS data, time in seconds since last SC104 type 1 or 9 update, null field when DGPS is not used
  //  14) Differential reference station ID, 0000-1023
  //  15) Checksum
    
  // Check if we have all the data necessary to proceed
  
  if ((NULL != ns) && (NULL != ew))
  {
    // Yes we do!

    // Clear down the buffer and add the talker ID
    
    nmeaBuffer[0] = '\0';
    strcat(&nmeaBuffer[0],talkerID);

    // Now add the GLL

    addNMEAZeroTerminatedString(false,NMEA_GGA);

    // 1) Universal Time Coordinated (UTC)

    addNMEATime(true,true);
    
    //  2) Latitude
    //  3) N or S (North or South)
    //  4) Longitude
    //  5) E or W (East or West)

    addNMEALatitudeLongitude(true);
    
    //  6) GPS Quality Indicator,
    //      1 - GPS fix,

    addNMEAInteger(true,1);

    //  7) Number of satellites in view, 00 - 12

    if (-1 != numberOfSatellites)
      addNMEAInteger(true,numberOfSatellites);
    else
      addNMEAZeroTerminatedString(true,""); 
      
    //  8) Horizontal Dilution of precision (meters)

    if (-1 != horizontalDillutionOfPosition)
      addNMEADouble(true,horizontalDillutionOfPosition,3,1);
    else
      addNMEAZeroTerminatedString(true,""); 
    
    //  9) Antenna Altitude above/below mean-sea-level (geoid) (in meters)

    addNMEAZeroTerminatedString(true,""); 
    
    //  10) Units of antenna altitude, meters

    addNMEAZeroTerminatedString(true,""); 
    
    //  11) Geoidal separation, the difference between the WGS-84 earth ellipsoid and mean-sea-level (geoid), "-" means mean-sea-level below ellipsoid
    
    addNMEAZeroTerminatedString(true,"");
    
    //  12) Units of geoidal separation, meters
    
    addNMEAZeroTerminatedString(true,"");
    
    //  13) Age of differential GPS data, time in seconds since last SC104 type 1 or 9 update, null field when DGPS is not used

    addNMEAZeroTerminatedString(true,"");
    
    //  14) Differential reference station ID, 0000-1023

    addNMEAZeroTerminatedString(true,"");
    
    //  15) Checksum
  
    addNMEACheckSum();

    // Send it out

    Serial.println(nmeaBuffer); 
  }
}

void generateGLL()
{
  //        1       2 3        4 5         6 7   8
  //        |       | |        | |         | |   |
  // $--GLL,llll.ll,a,yyyyy.yy,a,hhmmss.ss,a,m,*hh<CR><LF>
  //
  //  Field Number:
  //  1) Latitude
  //  2) N or S (North or South)
  //  3) Longitude
  //  4) E or W (East or West)
  //  5) Universal Time Coordinated (UTC)
  //  6) Status A - Data Valid, V - Data Invalid
  //  7) FAA mode indicator (NMEA 2.3 and later)
  //  8) Checksum
    
  // Check if we have all the data necessary to proceed
  
  if ((NULL != ns) && (NULL != ew))
  {
    // Yes we do!

    // Clear down the buffer and add the talker ID
    
    nmeaBuffer[0] = '\0';
    strcat(&nmeaBuffer[0],talkerID);

    // No add the GLL

    addNMEAZeroTerminatedString(false,NMEA_GLL);

    // Now add the latitude / longitude

    addNMEALatitudeLongitude(true);
    
    // Put in the time

    addNMEATime(true,true);

    // Put in the data valid

    addNMEAZeroTerminatedString(true,NMEA_STATUS_A);  

    // Add in the checksum
  
    addNMEACheckSum();

    // Send it out

    Serial.println(nmeaBuffer); 
  }
}

void generateMWV()
{
  //        1   2 3   4 5
  //        |   | |   | |
  // $--MWV,x.x,a,x.x,a*hh<CR><LF>
  //
  //  Field Number:
  //  1) Wind Angle, 0 to 360 degrees
  //  2) Reference, R = Relative, T = True
  //  3) Wind Speed
  //  4) Wind Speed Units, K/M/N
  //  5) Status, A = Data Valid
  //  6) Checksum
    
  // Check if we have all the data necessary to proceed
  
  if ((-1 != apparentWindAngle) || (-1 != apparentWindSpeed))
  {
    // Yes we do!

    // Clear down the buffer and add the talker ID
    
    nmeaBuffer[0] = '\0';
    strcat(&nmeaBuffer[0],talkerID);

    // No add the MWV

    addNMEAZeroTerminatedString(false,NMEA_MWV);

    if (-1 != apparentWindAngle)
    {
      // Put in the wind angle and relative indicator

      addNMEADouble(true,apparentWindAngle,3,1);
      addNMEAZeroTerminatedString(true,NMEA_RELATIVE);
    }
    else
    {
      // Put in blank wind angle and relative indicator
      
      addNMEAZeroTerminatedString(true,"");
      addNMEAZeroTerminatedString(true,"");
    }

    if (-1 != apparentWindSpeed)
    {
    // Put in the wind speed and unit indicator

    addNMEADouble(true,apparentWindSpeed,3,1);
    addNMEAZeroTerminatedString(true,"K"); 
    }
    else
    {
      // Put in blank wind wind speed and unit indicator
      
      addNMEAZeroTerminatedString(true,"");
      addNMEAZeroTerminatedString(true,"");      
    }

    // Put in the data valid

    addNMEAZeroTerminatedString(true,"A");  

    // Add in the checksum
  
    addNMEACheckSum();

    // Send it out

    Serial.println(nmeaBuffer);
  }
}

void generateRMC()
{
  //        1         2 3       4 5        6  7   8   9    10 11|  13
  //        |         | |       | |        |  |   |   |    |  | |   |
  // $--RMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,xxxx,x.x,a,m,*hh<CR><LF>  
  //
  //  Field Number:
  //  1) UTC Time
  //  2) Status, V=Navigation receiver warning A=Valid
  //  3) Latitude
  //  4) N or S
  //  5) Longitude
  //  6) E or W
  //  7) Speed over ground, knots
  //  8) Track made good, degrees true
  //  9) Date, ddmmyy
  //  10) Magnetic Variation, degrees
  //  11) E or W
  //  12) FAA mode indicator (NMEA 2.3 and later)
  //  13) Checksum

  // Clear down the buffer and add the talker ID

  nmeaBuffer[0] = '\0';
  strcat(&nmeaBuffer[0],talkerID);

  // Now add the RMC

  addNMEAZeroTerminatedString(false,NMEA_RMC);  

  // Add the time

  addNMEATime(true,true);

  // Add the status

  addNMEAZeroTerminatedString(true,NMEA_STATUS_A);  

  // Add the latitude / longitude

  addNMEALatitudeLongitude(true);
  
  // Add the speed in knots if we have it

  if (-1 != _speed)
  {
    // Add the speed in knots
      
    addNMEADouble(true,_speed,3,1);
  }
  else
  {
    // No speed in knots
      
    addNMEAZeroTerminatedString(true,"");
  } 

  // Add the track made good if we have it

  if (-1 != cog)
  {
    addNMEADouble(true,cog,3,1);
  }
  else
  {
    // No True track

    addNMEAZeroTerminatedString(true,"");
  }

  // Now add the date

  addNMEADate(true);

  // No magnetic variation

  addNMEAZeroTerminatedString(true,"");  
  addNMEAZeroTerminatedString(true,"");   

  // No FAA mode

  addNMEAZeroTerminatedString(true,"");   

  // Add in the checksum
  
  addNMEACheckSum();

  // Send it out

  Serial.println(nmeaBuffer);     
}

void generateVHW()
{
  //
  //        1   2 3   4 5   6 7   8 9
  //        |   | |   | |   | |   | |
  // $--VHW,x.x,T,x.x,M,x.x,N,x.x,K*hh<CR><LF>
  //
  //  Field Number:
  //  1) Degress True
  //  2) T = True
  //  3) Degrees Magnetic
  //  4) M = Magnetic
  //  5) Knots (speed of vessel relative to the water)
  //  6) N = Knots
  //  7) Kilometers (speed of vessel relative to the water)
  //  8) K = Kilometers
  //  9) Checksum

  if (-1 != speedThroughWater)
  {
    // Yes we do!

    // Clear down the buffer and add the talker ID
    
    nmeaBuffer[0] = '\0';
    strcat(&nmeaBuffer[0],talkerID);

    // Now add the VHW

    addNMEAZeroTerminatedString(false,NMEA_VHW);

    // Now degrees true

    addNMEAZeroTerminatedString(true,"");
    addNMEAZeroTerminatedString(true,"");      
    
    // No Magnetic track

    addNMEAZeroTerminatedString(true,"");
    addNMEAZeroTerminatedString(true,"");    

    // Add the speed in knots if we have it

    if (-1 != speedThroughWater)
    {
      // Add the speed through water in knots
      
      addNMEADouble(true,speedThroughWater,3,1);
      addNMEAZeroTerminatedString(true,"N");

      // Add the speed through water in kilometers / hour
    
      addNMEADouble(true,speedThroughWater * 1.852,3,1);
      addNMEAZeroTerminatedString(true,"K");
    }
    else
    {
      // No speed in knots
      
      addNMEAZeroTerminatedString(true,"");
      addNMEAZeroTerminatedString(true,"");  

      // No speed in kilometeres / hour
      
      addNMEAZeroTerminatedString(true,"");
      addNMEAZeroTerminatedString(true,"");  
    }

    // Put in blank FAA mode

    addNMEAZeroTerminatedString(true,""); 

    // Add in the checksum
  
    addNMEACheckSum();

    // Send it out

    Serial.println(nmeaBuffer);   
  }
}

void generateVTG()
{
  //         1  2  3  4  5  6  7  8 9   10
  //         |  |  |  |  |  |  |  | |   |
  // $--VTG,x.x,T,x.x,M,x.x,N,x.x,K,m,*hh<CR><LF>
  //
  //  Field Number:
  //  1) Track Degrees
  //  2) T = True
  //  3) Track Degrees
  //  4) M = Magnetic
  //  5) Speed Knots
  //  6) N = Knots
  //  7) Speed Kilometers Per Hour
  //  8) K = Kilometers Per Hour
  //  9) FAA mode indicator (NMEA 2.3 and later)
  //  10) Checksum 

 // Check if we have all the data necessary to proceed
  
  if ((-1 != _speed) || (-1 != cog))
  {
    // Yes we do!

    // Clear down the buffer and add the talker ID
    
    nmeaBuffer[0] = '\0';
    strcat(&nmeaBuffer[0],talkerID);

    // No add the VTG

    addNMEAZeroTerminatedString(false,NMEA_VTG);

    // No add the true track degrees if we have it

    if (-1 != cog)
    {
      addNMEADouble(true,cog,3,1);
      addNMEAZeroTerminatedString(true,"T");
    }
    else
    {
      // No True track

      addNMEAZeroTerminatedString(true,"");
      addNMEAZeroTerminatedString(true,"");      
    }

    // No Magnetic track

    addNMEAZeroTerminatedString(true,"");
    addNMEAZeroTerminatedString(true,"");    

    // Add the speed in knots if we have it

    if (-1 != _speed)
    {
      // Add the speed in knots
      
      addNMEADouble(true,_speed,3,1);
      addNMEAZeroTerminatedString(true,"N");

      // Add the speed in kilometers / hour
    
      addNMEADouble(true,_speed * 1.852,3,1);
      addNMEAZeroTerminatedString(true,"K");
    }
    else
    {
      // No speed in knots
      
      addNMEAZeroTerminatedString(true,"");
      addNMEAZeroTerminatedString(true,"");  

      // No speed in kilometeres / hour
      
      addNMEAZeroTerminatedString(true,"");
      addNMEAZeroTerminatedString(true,"");  
    }

    // Put in blank FAA mode

    addNMEAZeroTerminatedString(true,""); 

    // Add in the checksum
  
    addNMEACheckSum();

    // Send it out

    Serial.println(nmeaBuffer); 
  }   
}

void generateZDA()
{
  //        1         2  3  4    5  6  7
  //        |         |  |  |    |  |  |
  // $--ZDA,hhmmss.ss,xx,xx,xxxx,xx,xx*hh<CR><LF>  
  //
  //  Field Number:
  //  1) UTC time (hours, minutes, seconds, may have fractional subsecond)
  //  2) Day, 01 to 31
  //  3) Month, 01 to 12
  //  4) Year (4 digits)
  //  5) Local zone description, 00 to +- 13 hours
  //  6) Local zone minutes description, apply same sign as local hours
  //  7) Checksum

    // Clear down the buffer and add the talker ID

  nmeaBuffer[0] = '\0';
  strcat(&nmeaBuffer[0],talkerID);

  // Now add the ZDA

  addNMEAZeroTerminatedString(false,NMEA_ZDA);  

  // Add the time

  addNMEATime(true,true);

  // Add the day

  addNMEAZeroTerminatedString(true,""); 
  
  if (-1 != day)
  {
    if (day < 10)
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],"0");
    itoa(day,&nmeaBuffer[strlen(nmeaBuffer)],10);   
  }

  // Add the month

  addNMEAZeroTerminatedString(true,""); 
    
  if (-1 != month)
  {
    if (month < 10)
      strcat(&nmeaBuffer[strlen(nmeaBuffer)],"0");
    itoa(month,&nmeaBuffer[strlen(nmeaBuffer)],10);   
  }
  
  // Add the year

  addNMEAZeroTerminatedString(true,""); 
    
  if (-1 != year)
  {
    itoa(year + 2000,&nmeaBuffer[strlen(nmeaBuffer)],10);   
  }  

  // Add the local time zone

  addNMEAZeroTerminatedString(true,"00");

  // Add the local time zone minutes

  addNMEAZeroTerminatedString(true,"00"); 

  // Add in the checksum
  
  addNMEACheckSum();

  // Send it out

  Serial.println(nmeaBuffer);     
}

void translateDataToNMEA() {

  double seatalkDegrees;
  double seatalkMinutes;
  int RST;
  uint16_t yyyy;
  uint16_t xxxx;
  int U;
  int VW;
  byte c1, c2, c3, c4;
    
  switch(b[0])
  {
    case SEATALK_DEPTH_BELOW_TRANSDUCER:

      // 0   1   2   3  4
      // 00  02  YZ  XX XX  Depth below transducer: XXXX/10 feet 
      //                      Flags in Y: Y&8 = 8: Anchor Alarm is active
      //                                  Y&4 = 4: Metric display units or
      //                                           Fathom display units if followed by command 65
      //                                  Y&2 = 2: Used, unknown meaning
      //                      Flags in Z: Z&4 = 4: Transducer defective
      //                                  Z&2 = 2: Deep Alarm is active
      //                                  Z&1 = 1: Shallow Depth Alarm is active
      //                    Corresponding NMEA sentences: DPT, DBT

      // Bug Fix suggested by Jan Dytrych
      //
      // depthBelowTransducer = ((b[3] * 256 + b[4]) / 10) * FEET_TO_METERS;
      
      depthBelowTransducer = ((b[4] * 256 + b[3]) / 10) * FEET_TO_METERS;

      // Generate an NMEA as required.

      generateDBT();
      generateDPT();
      break;
                    
    case SEATALK_APPARENT_WIND_ANGLE:

      // 10  01  XX  YY  Apparent Wind Angle: XXYY/2 degrees right of bow 
      //                 Used for autopilots Vane Mode (WindTrim) 
      //                 Corresponding NMEA sentence: MWV
                
      apparentWindAngle = ((double)b[2] * 256 + (double)b[3]) / 2.0;

      // Generate an NMEA MWV as required.

      generateMWV();
      break;

    case SEATALK_APPARENT_WIND_SPEED:

      // 11  01  XX  0Y  Apparent Wind Speed: (XX & 0x7F) + Y/10 Knots 
      //                 Units flag: XX&0x80=0    => Display value in Knots 
      //                             XX&0x80=0x80 => Display value in Meter/Second 
      //                 Corresponding NMEA sentence: MWV
                
      apparentWindSpeed = (b[2] & 0x7f) + (b[3] & 0xf) / 10;

      // Generate an NMEA MWV as required     
                             
      generateMWV();
      break;

    case SEATALK_SPEED_THROUGH_WATER:

      // 20  01  XX  XX  Speed through water: XXXX/10 Knots 
      //                 Corresponding NMEA sentence: VHW

      speedThroughWater = ((double)(b[3] * 256 + b[2])) / 10.0;
      
      // Generate an NMEA VHW as required     
                             
      generateVHW();
      break;
         
    case SEATALK_COG:

      // 53  U0  VW      Course over Ground (COG) in degrees: 
      //                 The two lower  bits of  U * 90 + 
      //                    the six lower  bits of VW *  2 + 
      //                    the two higher bits of  U /  2 = 
      //                    (U & 0x3) * 90 + (VW & 0x3F) * 2 + (U & 0xC) / 8
      //                 The Magnetic Course may be offset by the Compass Variation (see datagram 99) 
      //                 to get the Course Over Ground (COG). 
      //                 Corresponding NMEA sentences: RMC, VTG

      U = (b[1] & 0xf0) >> 4;
      VW = b[2];
      cog = (U & 0x3) * 90 + (VW & 0x3F) * 2 + ((U & 0xC) >> 2) / 2;

      // Generate sentences as required

      generateRMC();
      generateVTG();
      break;
                
    case SEATALK_DATE:

      //  56  M1  DD  YY  Date: YY year, M month, DD day in month 
      //                  Corresponding NMEA sentence: RMC

      day = b[2];
      month = (b[1] & 0xf0) >> 4;
      year = b[3];

      // Generate sentences as required
      
      generateRMC();
      generateZDA();
                
      break;

    case SEATALK_SATINFO:

      //  0   1   2  
      //  57  S0  DD      Sat Info: S number of sats, DD horiz. dillution of position, if S=1 -> DD=0x94
      //                  Corresponding NMEA sentences: GGA, GSA    

      numberOfSatellites = b[1] >> 4;
      if (1 == numberOfSatellites)
        horizontalDillutionOfPosition = 0x94;
      else
        horizontalDillutionOfPosition = (double)b[2];

      // Generate sentences as required
           
      generateGGA();
      break;

    case SEATALK_LATITUDE:

      //  50  Z2  XX  YY  YY  LAT position: XX degrees, (YYYY & 0x7FFF)/100 minutes 
      //                      MSB of Y = YYYY & 0x8000 = South if set, North if cleared
      //                      Z= 0xA or 0x0 (reported for Raystar 120 GPS), meaning unknown
      //                            Stable filtered position, for raw data use command 58
      //                      Corresponding NMEA sentences: RMC, GAA, GLL

      seatalkDegrees = b[2];
      yyyy = b[4] * 256 + b[3];
      seatalkMinutes = ((double)(yyyy & 0x7fff)) / 100;

      // Set up the NMEA equivalent
                
      latitude = seatalkDegrees * 100 + seatalkMinutes;
      ns = (yyyy & 0x8000) == 0 ? (char*)"N" : (char*)"S";

      // Generate sentences as required
                     
      generateRMC();     
      generateGGA();
      generateGLL();  
      break;

    case SEATALK_LONGITUDE:

      //  51  Z2  XX  YY  YY  LON position: XX degrees, (YYYY & 0x7FFF)/100 minutes 
      //                      MSB of Y = YYYY & 0x8000 = East if set, West if cleared 
      //                      Z= 0xA or 0x0 (reported for Raystar 120 GPS), meaning unknown
      //                            Stable filtered position, for raw data use command 58
      //                      Corresponding NMEA sentences: RMC, GAA, GLL

      seatalkDegrees = b[2];
      yyyy = b[4] * 256 + b[3];
      seatalkMinutes = ((double)(yyyy & 0x7fff)) / 100;

      // Set up the NMEA equivalent
                
      longitude = seatalkDegrees * 100 + seatalkMinutes;
      ew = (yyyy & 0x8000) == 0 ? (char*)"W" : (char*)"E";

      // Generate sentences required
                
      generateRMC(); 
      generateGGA();      
      generateGLL();         
      break;                

    case SEATALK_SPEED:

      //  52  01  XX  XX  Speed over Ground: XXXX/10 Knots 
      //                  Corresponding NMEA sentences: RMC, VTG

      _speed = (b[3] * 256 + b[2]) / 10.0;

      // Generate sentences required
      
      generateRMC();
      generateVTG();
      break;

    case SEATALK_LATLON:

      //  0   1   2  3  4  5  6  7
      //  58  Z5  LA XX YY LO QQ RR   LAT/LON 
      //                  LA Degrees LAT, LO Degrees LON
      //                  minutes LAT = (XX*256+YY) / 1000 
      //                  minutes LON = (QQ*256+RR) / 1000 
      //                  Z&1: South (Z&1 = 0: North) 
      //                  Z&2: East  (Z&2 = 0: West)
      //                  Raw unfiltered position, for filtered data use commands 50&51
      //                  Corresponding NMEA sentences: RMC, GAA, GLL    
      
      latitude = (double)b[2] * 100 + (((double)b[3] * 256 + (double)b[4]) / 1000);
      longitude = (double)b[5] * 100 + (((double)b[6] * 256 + (double)b[7]) / 1000);
      ns = (b[1] & (1 << 4)) == 0 ? (char*)"N" : (char*)"S";
      ew = (b[1] & (1 << 5)) == 0 ? (char*)"W" : (char*)"E";

      // Generate sentences required

      generateRMC();
      generateGGA();
      generateGLL();
      break;  

    //case SEATALK_TARGET_WAYPOINT_NAME:

      //  82  05  XX  xx YY yy ZZ zz   Target waypoint name 
      //                  XX+xx = YY+yy = ZZ+zz = FF (allows error detection) 
      //                  Takes the last 4 chars of name, assumes upper case only 
      //                  Char= ASCII-Char - 0x30 
      //                  XX&0x3F: char1 
      //                  (YY&0xF)*4+(XX&0xC0)/64: char2 
      //                  (ZZ&0x3)*16+(YY&0xF0)/16: char3 
      //                  (ZZ&0xFC)/4: char4 
      //                  Corresponding NMEA sentences: RMB, APB, BWR, BWC
                 
      //c1 = b[2] & 0x3f; c1 += 0x30;
      //c2 = (b[4] & 0xf) * 4 + (b[4] & 0xc0) / 64; c2 += 0x30;
      //c3 = (b[6] & 0x3) * 16 + (b[4] & 0xf0) / 16; c3 += 0x30;
      //c4 = (b[6] & 0xfc) / 4; c4 += 0x30;

      //Serial.print((char)c1);
      //Serial.print((char)c2);
      //Serial.print((char)c3);
      //Serial.print((char)c4);
      //break;
      
    case SEATALK_TIME:

      //  54  T1  RS  HH  GMT-time: HH hours, 
      //                            6 MSBits of RST = minutes = (RS & 0xFC) / 4
      //                            6 LSBits of RST = seconds =  ST & 0x3F 
      //                  Corresponding NMEA sentences: RMC, GAA, BWR, BWC

      hours = b[3];
      RST = (b[2] << 4) | ((b[1] & 0xf0) >> 4);
      minutes = (b[2] & 0xfc) / 4;
      seconds = RST & 0x3f;

      // Generate sentences as required

      generateRMC();
      generateGGA();
      generateZDA();

      break;
                
    default:

      // This is a SeaTalk sentence that we are not processing. Log it!

      formUnhandledProprietaryNMEA();
      break;
    }
}

void setup() {

  // Setup the serial port
  
  Serial.begin(9600);
  while (!Serial) { } // wait for serial port to connect. Needed for native USB port only

  // Setup the EEPROM. 

  byte eepromMajor = EEPROM.read(EEPROM_MAJOR_VERSION);
  byte eepromMinor = EEPROM.read(EEPROM_MINOR_VERSION);

  if ((MAJOR_VERSION != eepromMajor) || (MINOR_VERSION != eepromMinor))
  {
    Serial.println("Initialising EEPROM...");  

    // Set the version numbers

    EEPROM.write(EEPROM_MAJOR_VERSION,MAJOR_VERSION);
    EEPROM.write(EEPROM_MINOR_VERSION,MINOR_VERSION);

    // Set the default Talker ID

    EEPROM.write(EEPROM_TALKERID_1,(byte)NMEA_DEFAULT_TALKERID_1);
    EEPROM.write(EEPROM_TALKERID_2,(byte)NMEA_DEFAULT_TALKERID_2);
  }

  // Load the defaults from the EEPROM

  talkerID[1] = (char)EEPROM.read(EEPROM_TALKERID_1);
  talkerID[2] = (char)EEPROM.read(EEPROM_TALKERID_2);  

  // Load the debug seatalk flag
  
  debugSeatalk = EEPROM.read(EEPROM_DEBUG_SEATALK) > 0;

  // Initialise the command buffer;

  commandBufferIndex = 0;

  // Open the SeaTalk serial port.
  
  Serial1.begin(HS_BAUD_RATE,true);

  // Send the version sentence

  sendNMEAVersionSentence();

  // Initialise the buffer

  bi = 0;
  processingPacket = false;

#ifdef DEBUG

  // Test Data

  // $PIMSST1,D,56,21,1b,12,*25

  b[0] = 0x56;
  b[1] = 0x21;
  b[2] = 0x1b;
  b[3] = 0x12;

  translateDataToNMEA();
  
  // $PIMSST1,D,10,1,1,1,*45

  b[0] = 0x10;
  b[1] = 0x01;
  b[2] = 0x01;
  b[3] = 0x01;

  translateDataToNMEA();

  // $PIMSST1,D,54,c1,80,e,*4b

  b[0] = 0x54;
  b[1] = 0xc1;
  b[2] = 0x80;
  b[3] = 0x0e;
  
  translateDataToNMEA();

  // $PIMSST1,D,11,1,2,2,*44
  
  b[0] = 0x11;
  b[1] = 0x01;
  b[2] = 0x02;
  b[3] = 0x02;
  
  translateDataToNMEA();

  // $PIMSST1,D,53,90,1e,*02

  b[0] = 0x53;
  b[1] = 0x90;
  b[2] = 0x1e;
  
  translateDataToNMEA();

  // $PIMSST1,D,50,2,33,53,17,*6e

  b[0] = 0x50;
  b[1] = 0x02;
  b[2] = 0x33;
  b[3] = 0x53;    
  b[4] = 0x17;   

  translateDataToNMEA();   

  // $PIMSST1,D,51,2,1,5f,86,*03  

  b[0] = 0x51;
  b[1] = 0x02;
  b[2] = 0x01;
  b[3] = 0x5f;    
  b[4] = 0x86;   

  translateDataToNMEA();

  // $PIMSST1,D,52,1,0,0,*43

  b[0] = 0x52;
  b[1] = 0x02;
  b[2] = 0x00;
  b[3] = 0x00; 

  translateDataToNMEA();     

  // $PIMSST1,D,56,41,a,12*3d

  b[0] = 0x56;
  b[1] = 0x41;
  b[2] = 0xa;
  b[3] = 0x12;

  translateDataToNMEA();   

  // $PIMSST1,D,54,91,cf,8*6d

  b[0] = 0x54;
  b[1] = 0x91;
  b[2] = 0xcf;
  b[3] = 0x08;

  translateDataToNMEA();    

  // $PIMSST1,D,82,5,40,bf,e5,1a,51,ae*66

  b[0] = 0x82;
  b[1] = 0x5;
  b[2] = 0x40;
  b[3] = 0xbf;
  b[4] = 0xe5;
  b[5] = 0x1a;
  b[6] = 0x51;
  b[7] = 0xae;

  translateDataToNMEA();  

  // $PIMSST1,D,82,5,40,bf,e5,1a,5,65*50

  b[0] = 0x82;
  b[1] = 0x5;
  b[2] = 0x40;
  b[3] = 0xbf;
  b[4] = 0xe5;
  b[5] = 0x1a;
  b[6] = 0x5;
  b[7] = 0x65;

  translateDataToNMEA();    

  // $PIMSST1,D,58,25,33,e7,ea,1,31,12*35
  
  b[0] = 0x58;
  b[1] = 0x25;
  b[2] = 0x33;
  b[3] = 0xe7;
  b[4] = 0xea;
  b[5] = 0x1;
  b[6] = 0x31;
  b[7] = 0x12;  

  translateDataToNMEA();    

  // $PIMSST1,D,57,10,14*73

  b[0] = 0x57;
  b[1] = 0x10;
  b[2] = 0x14;  

  packetLength = 3;
  translateDataToNMEA(); 

  // $PIMSST1,D,82,5,0,ff,0,ff,c,f3*50

  b[0] = 0x82;
  b[1] = 0x5;
  b[2] = 0x0;
  b[3] = 0xff;
  b[4] = 0x0;
  b[5] = 0xff;
  b[6] = 0xc;
  b[7] = 0xf3;   

  translateDataToNMEA();   

  // DEPTH!
  
  b[0] = 0x00;
  b[1] = 0x02;
  b[2] = 0x00;
  b[3] = 0x00;
  b[4] = 0x32;

  translateDataToNMEA();

  // $PIMSST1,D,57,50,2*40
  
  b[0] = 0x57;
  b[1] = 0x50;
  b[2] = 0x2;

  translateDataToNMEA();
  
#endif
}

void processCommand()
{
  switch(commandBuffer[0])
  {
    case COMMAND_HELP_1:
    case COMMAND_HELP_2:
      Serial.print(PRODUCT_NAME);
      Serial.print(" V");
      Serial.print(MAJOR_VERSION);
      Serial.print(".");
      Serial.println(MINOR_VERSION);
      Serial.println("Available commands:");
      Serial.println("    D{T|F} - Debug True or False. For example DT");
      Serial.println("    Tcc    - Set TalkerID to cc. For example TGP");
      break;
      
    case COMMAND_DEBUG:

      if (2 == commandBufferIndex)
      {
        switch(commandBuffer[1])
        {
          case COMMAND_TRUE:
            EEPROM.write(EEPROM_DEBUG_SEATALK,1);
            break;

          case COMMAND_FALSE:
            EEPROM.write(EEPROM_DEBUG_SEATALK,0);
            break;

          default:
            Serial.print("Invalid debug option: ");
            Serial.println(commandBuffer[1]);
            break;
        }

        debugSeatalk = EEPROM.read(EEPROM_DEBUG_SEATALK) > 0;
        Serial.print("Debug set to: ");
        Serial.println(true == debugSeatalk ? "On" : "Off");
      }
      else
        Serial.println("Badly formed command!");
      break;
      
    case COMMAND_TALKERID:

      if (3 == commandBufferIndex)
      {
        // Set the default Talker ID

        EEPROM.write(EEPROM_TALKERID_1,commandBuffer[1]);
        EEPROM.write(EEPROM_TALKERID_2,commandBuffer[2]);
 
        // Load the defaults from the EEPROM

        talkerID[1] = (char)EEPROM.read(EEPROM_TALKERID_1);
        talkerID[2] = (char)EEPROM.read(EEPROM_TALKERID_2);  

        // Display the talker ID

        Serial.print("Talker ID set to: ");
        Serial.println(talkerID);
      }
      else
        Serial.println("Badly formed command!");
      break;
      
    default:
      Serial.print("Unrecognised command: ");
      Serial.println(commandBuffer);
  }

  commandBufferIndex = 0;
}
void loop() {

  // Do we have a command to be processed?

  while (0 != Serial.available())
  {
    byte commandByte = Serial.read();
    
    switch(commandByte)
    {
      case '\r':
      case '\n':
        commandBuffer[commandBufferIndex] = '\0';

        // We have a command that we can process!

        if (commandBufferIndex > 0)
          processCommand();
        break;
        
      default:

        if (commandBufferIndex == COMMAND_BUFFER_LENGTH)
          commandBufferIndex = 0;

        commandBuffer[commandBufferIndex] = commandByte;
        commandBufferIndex++;
        break;
    }
  }
  
  // We are expecting that all SeaTalk packets begin with a command.

  while (0 != Serial1.available ())
  {
    // Read the character. 
    
    int v = Serial1.read() & 0x1FF;

    // What we do next depends on our state.

    if (false == processingPacket)
    {
      // We are currently not processing a packet so we need to look
      // for the start of a command. If this is not a command then
      // ignore it.

      if ((v & 0x100) > 0)
      {
        // This is a command, save it away in the buffer and set the flags to indicate
        // that we are mid packet.

        b[bi++] = v & 0xff;
        processingPacket = true;
      }
    }
    else
    {
      // OK, we're mid packet. If we get a command at this point then we need to start
      // processing again. We may get this if a collision has happened.

      if ((v & 0x100) > 0)
      {
        // This is a command, reset the buffer and save it away in the buffer and set the 
        // flags to indicate that we are mid packet.

        bi = 0;
        b[bi++] = v & 0xff;
      }
      else
      {
        // OK, this is a valid character. It may be the attribute character providing the packet length
        //
        //  Attribute Character, specifying the total length of the datagram in the least significant nibble:
        //
        //                    Most  significant 4 bits: 0 or part of a data value 
        //                    Least significant 4 bits: Number of additional data bytes = n  => 
        //                    Total length of datagram = 3 + n  characters

        if (1 == bi) 
        {
          // This is the packet length

          b[bi++] = v;
          packetLength = 3 + (v & 0x0f);
        }
        else
        {
          // This is simple data, save it away

          b[bi++] = v;

          // We can now check to see if we have a full packet

          if (packetLength == bi)
          {
            // We have a full packet. Form the proprietary message and display it.

            if (true == debugSeatalk)
            {
              formProprietaryNMEA();
              Serial.println(nmeaBuffer);  
            }

            // At this point we can look at the message that we have and decide 
            // if we want to do something with it.

            translateDataToNMEA();
            
            // Reset the buffer

            bi = 0;
            processingPacket = false;
          }
        }
      }
    }
  }
}
