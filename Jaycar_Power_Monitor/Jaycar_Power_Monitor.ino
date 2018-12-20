/* ********************************************************************
 *  Power Usage Monitor
 *  
 *  Please goto www.jaycar.com.au for project documentation and details
 *  
 *  Code published: v1.0 4/April/2018
 *  
 * *********************************************************************
 */ 

// This is where all the libraries are included
#include <time.h>
#include <ESP8266WiFi.h>
#include "Gsender.h"
#include <WiFiUdp.h>
#include "ACS712.h"


// Program literal definitions
#define DC_current    0               // constant types used to switch between DC and AC measurements
#define AC_current    1               // see measurement_type variable below

#define to_email      "your target email account"   // in the closed brackets enter your target email address, e.g. "john@gmail.com"

#define timezone            11        // Time zone including day light savings
#define dst                 1         // There is a bug in the time synch, day light savings does not work
#define sample_size         5         // number of readings in each smaple block, used to calculate average reading
#define reading_interval    30000     // 30000 ms between each reading
#define daily_usage_limit  1000      // This is the daily usage limit



// Define the gloabl variables here

// --- WiFi related variable
#pragma region Globals
const char* ssid = "WAVLINK_012B";               // WIFI network name. Edit this filed to your WiFi network SSID 
const char* password = "lab12345";               // WIFI network password. Edit this firled to your WiFi network password
uint8_t connection_state = 0;                    // Connected to WIFI or not
uint16_t reconnect_interval = 10000;             // If not connected wait time to try again
#pragma endregion Globals

// -- Other program related variables
time_t start_time;
long interval_t;
int i = 0;                            // this variable is used to track the reading sequence # [0..29]
int measurment_type = DC_current;     // change this to the type of current DC or AC
bool  reported_25p;
bool  reported_50p;
bool  reported_max;
float user_limit_max;
float user_limit_25p;
float user_limit_50p;
float reading[sample_size];                    // keeps record of last 30 measurement readings
float average_reading;
float daily_usage;
float Vac = 12;                               // Edit this value to match the voltage level in your application.
float Vdc = 12;                                // Edit this value to match the voltage level in your application.

// Configure the 30 amps version sensor connected to A0 of the ESP8266
ACS712 sensor(ACS712_30A, A0); 
 
/*
 * This function establish the WiFi network connection. Return True (good connection) or False (could not connect)
 */
uint8_t WiFiConnect(const char* nSSID = nullptr, const char* nPassword = nullptr)
{
    static uint16_t attempt = 0;
    Serial.print(F("Connecting to "));
    if(nSSID) {
        WiFi.begin(nSSID, nPassword);  
        Serial.println(nSSID);
    } else {
        WiFi.begin(ssid, password);
        Serial.println(ssid);
    }

    uint8_t i = 0;
    while(WiFi.status()!= WL_CONNECTED && i++ < 50)
    {
        delay(200);
        Serial.print(".");
    }
    ++attempt;
    Serial.println("");
    if(i == 51) {
        Serial.print(F("Connection: TIMEOUT on attempt: "));
        Serial.println(attempt);
        if(attempt % 2 == 0)
            Serial.println(F("Check if access point available or SSID and Password\r\n"));
        return false;
    }
    Serial.println(F("Connection: ESTABLISHED"));
    Serial.print(F("IP address: "));
    Serial.println(WiFi.localIP());
    return true;
}


void Awaits()
{
    uint32_t ts = millis();
    while(!connection_state)
    {
        delay(50);
        if(millis() > (ts + reconnect_interval) && !connection_state){
            connection_state = WiFiConnect();
            ts = millis();
        }
    }
}

bool send_gmail_notification(String type, float reading_value) {

      int n;    
      Gsender *gsender = Gsender::Instance();    // Getting pointer to class instance
      time_t  report_time = time(nullptr);

      // Set the email subject line
      String subject = "Power Monitor Notification";

      // construct the email body content
 
      String message = "Hi,<br>";
      
      if (type == "25p") {
            message += " WARNING: 25% of daily usage limit reached<br>";
      } else if (type == "50p") {
            message += " WARNING: 50% of daily usage limit reached<br>";
      } else if (type == "max") {
            message += " Daily usage limit reached<br>";
      } else if (type == "day") {
            message += " Daily usage report";
      }

      message += "Monitoring From: ";
      message += String(localtime(&start_time)->tm_mday)+"/"+String(localtime(&start_time)->tm_mon+1)+"/"+String(localtime(&start_time)->tm_year+1900);
      message += "       "+String(localtime(&start_time)->tm_hour)+":"+String(localtime(&start_time)->tm_min)+":"+String(localtime(&start_time)->tm_sec);

      message += "<br>";
      message += "Monitoring To: ";
      message += String(localtime(&report_time)->tm_mday)+"/"+String(localtime(&report_time)->tm_mon+1)+"/"+String(localtime(&report_time)->tm_year+1900);
      message += "       "+String(localtime(&report_time)->tm_hour)+":"+String(localtime(&report_time)->tm_min)+":"+String(localtime(&report_time)->tm_sec);
      message += "<br>";

      if (measurment_type == DC_current) {
          message += "User set daily limit : " + String (daily_usage_limit) + " Watts    @ DC voltage: " + String(Vdc) + " V";
          message += "Power Consumption: "+ String (reading_value * Vdc) + " Watts";
      } else {
          message += "User set daily limit : " + String (daily_usage_limit) + " Watts    @ AC voltage: " + String(Vac) + " V";
          message += "Power Consumption: "+ String (reading_value * Vdc) + " Watts";
      }

      message += "<br><br>";
      message += "---------------------------------------------------------------------------------------------- <br>";
      message += "NOTE: These readings are only indicative, and should not be used for billing or accounting purpose";
      message += "---------------------------------------------------------------------------------------------- <br>";
      
      if(gsender->Subject(subject)->Send(to_email, message))
          return true;
      else
          return false;
          
}

float calculate_average() {

      int num_zero = 0;
      float average = 0; 

      for (int n=0; n< sample_size; n++) {
        
        average = average + reading[n];         // accumulate the sum of readings
        
        if (reading[n] <= 2)                    // ignore noise readings
            num_zero++;     
      }

      if (num_zero >= sample_size * 0.3)      // if 30% of samples are zero, exclude the readings interval
        return 0;
      else
        return average / sample_size;
        
}


void reset_global_variables () {

  i = 0;
  daily_usage = 0;
  
  reported_25p = false;
  reported_50p = false;
  reported_max = false;
  
  for (int n=0; n < sample_size; n++)
       reading[n] = 0;
       
  if (measurment_type == DC_current) {
        user_limit_max = daily_usage_limit / Vdc;
        user_limit_25p = 0.25 * user_limit_max;
        user_limit_50p = 0.50 * user_limit_max;  
  } else {
        user_limit_max = daily_usage_limit / Vac;
        user_limit_25p = 0.25 * user_limit_max;
        user_limit_50p = 0.50 * user_limit_max;  
  }

}

void setup() {
          
    Serial.begin(115200);

    // Conect to the WiFi network
    connection_state = WiFiConnect();
    if(!connection_state)  // if not connected to WIFI
        Awaits();          // constantly trying to connect

    // Synchronise the time and date
    configTime(timezone * 3600, dst * 3600, "pool.ntp.org", "time.nist.gov");
    while (!time(nullptr)) {
      Serial.println(".");
      delay(1000);
    }

    // This method calibrates zero point of sensor. Ensure that no current flows through the sensor at this moment
    sensor.calibrate();


    reset_global_variables();

    start_time = time(nullptr);
    interval_t = millis();

}

/*
 * This is the main program loop. It continues to loop forvever and ever 
 */
void loop() {

    if (millis() - interval_t > reading_interval) {
        if (measurment_type == DC_current) {
              reading[i] =  sensor.getCurrentDC(); // take a current measurement (default 50Hz AC)    
            } else {
              reading[i] = sensor.getCurrentAC(); // take a current measurement (default 50Hz AC)   
            }

        if (i++ > sample_size) {        // This does two things, it increments the counter i, and compares it to sample_size
          i = 0;
          
          average_reading =  calculate_average();
          daily_usage = daily_usage + average_reading; 
          
          if (average_reading > user_limit_25p && average_reading < user_limit_50p && reported_25p == false) {
                send_gmail_notification("25p",average_reading);
                reported_25p = true;
          } else if (average_reading > user_limit_50p && average_reading < user_limit_max && reported_50p == false) {
                send_gmail_notification("50p",average_reading);
                reported_50p = true;
          } else if (average_reading > user_limit_max && reported_max == false) {
                send_gmail_notification("max",average_reading);
                reported_max = true;
          }
           
        } 

      time_t time_now = time(nullptr);
      
      if ( localtime(&time_now)->tm_mday > localtime(&start_time)->tm_mday ) {
            send_gmail_notification("day",daily_usage);     // Send Daily usage 
            reset_global_variables();                         // reset parameters and start a new day measurements
            start_time = time(nullptr);
      } 

        
        interval_t = millis();
    }
     
}      


