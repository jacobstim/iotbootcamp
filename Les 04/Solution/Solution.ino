/*
  Connected BME680 sensor.

  Connects to an Azure IoT Hub, transfers the data.

  Code by Tim Jacobs (2019)
*/

// Use Azure IoT Hub Baltimore Root Certificate Authority for IoT Hub SSL certificate validation
#define USE_BALTIMORE_CERT

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include "AzureCerts.h"

/* **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * First, we include the necessary libraries: 
 *  - Wire for I2C support
 *  - Adafruit Sensor as a prerequisite for Adafruit_BME680 library
 *  - Adafruit BME680 to read out the BME680 sensor
 *  
 *  Next, we define the bme object to communicate with the sensor
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 */
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"

#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME680 bme; // I2C

 /* ====================================================================================================
  * Configuration
  * ==================================================================================================== */

const String wifi_ssid = "IP_TiJa";
const String wifi_pass = "123456789";

const String iotHubHost   = "iotbootcamp-hub01.azure-devices.net";
const uint16_t iotHubPort   = 443;

const String iotDeviceId = "tija-huzzah01";
const String iotPassword = "SharedAccessSignature sr=iotbootcamp-hub01.azure-devices.net%2Fdevices%2Ftija-huzzah01&sig=7FDzBb7syroar4ZFLgy6g5FAut8rd7bDAwid8NsGUCQ%3D&se=1603729447";
const String iotHubURL = "https://" + iotHubHost + "/devices/" + iotDeviceId + "/messages/events?api-version=2016-11-14";

 /* ====================================================================================================
  * Constants
  * ==================================================================================================== */
 
typedef enum { 
  WIFI_DISCONNECTED,
  WIFI_CONNECTING,
  WIFI_APFOUND,
  WIFI_CONNECTED
} wifiStatusCode;

#define YELLOW_LED LED_BUILTIN
#define WIFI_LED LED_BUILTIN

 /* ====================================================================================================
  * Global Variables
  * ==================================================================================================== */

wifiStatusCode wifiState = WIFI_DISCONNECTED;
WiFiEventHandler mConnectHandler;
WiFiEventHandler mDisConnectHandler;
WiFiEventHandler mGotIpHandler;

// Counter for WiFi connection attempts
#define WIFI_TIMEOUT 10000                              // Abort connection attempt after 10 seconds
uint32_t wifiStartToConnectTime = -WIFI_TIMEOUT;

// Counter for NTP time syncs
#define NTP_TIMEOUT 900000                              // NTP sync every 15 mins
uint32_t ntpLastSync = -NTP_TIMEOUT;

// Counter for data measurements
#define MEASUREMENT_INTERVAL 5000                       // Measurement interval in milliseconds
uint32_t dataLastMeasurementTime = -MEASUREMENT_INTERVAL;

// WiFi SSL clients
BearSSL::WiFiClientSecure wifiClient;
//WiFiClientSecure wifiClient;
BearSSL::X509List msftCerts(certificates);

 /* ====================================================================================================
  * Setup 
  * ==================================================================================================== */
  
void setup() {
  // Prepare for debug output
  Serial.begin(115200);
  //Serial.setDebugOutput(true);                    // Dump ESP8266 debug output here
  
  Serial.println("Connected BME680 weather station v1.0");
    
  // initialize LED's and turn them off
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, 0);

  // Purge whatever WiFi settings are active
  Serial.println("Preparing WiFi...");
  WiFi.disconnect() ;
  WiFi.persistent(false);

  // Initialize WiFi event handlers
  mConnectHandler = WiFi.onStationModeConnected(onConnected);
  mDisConnectHandler = WiFi.onStationModeDisconnected(onDisconnect);
  mGotIpHandler = WiFi.onStationModeGotIP(onGotIP);

  // Configure Secure WiFi client to accept Microsoft certificates
  wifiClient.setTrustAnchors(&msftCerts);
  //wifiClient.setInsecure();     // Accept any certificate

/* **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * Here, we initialize the BME680 sensor and configure it for reading data
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 */

  Serial.println("Preparing BME680 sensor...");
  if (!bme.begin()) {
    Serial.println("-> ERROR! Could not find a valid BME680 sensor, check wiring!");
    while (1);
  }

  // Set up oversampling and filter initialization
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms

  Serial.println("Setup done!");
}


 /* ====================================================================================================
  * Loop
  * ==================================================================================================== */
  
void loop() {
  uint32_t currentTime = millis();

  // ---------------------------------------------------------------------------------------------------
  // WIFI HEALTH KEEPING
  // ---------------------------------------------------------------------------------------------------
  // Are we still connected to WiFi?
  if (WiFi.status() != WL_CONNECTED) {
    // We are NOT connected to WiFi... 
    // Are we attempting to connecting?
    if ((wifiState == WIFI_CONNECTING) && (currentTime > wifiStartToConnectTime + WIFI_TIMEOUT )) {
      // Connection attempt failed... retry
      Serial.println ( "WiFi - Unable to connect!" );
      wifiState = WIFI_DISCONNECTED;
    }
    // Are we just disconnected?
    if (wifiState == WIFI_DISCONNECTED) {
      // Not connected, try to connect
      digitalWrite ( WIFI_LED, 1 );
      wifiStartToConnectTime = currentTime;
      
      wifiState = WIFI_CONNECTING;
      WiFi.disconnect() ;
      WiFi.mode(WIFI_STA); 
      WiFi.begin ( wifi_ssid, wifi_pass );  
      Serial.println ( "WiFi - Attempting to connect..." );
    }
  } else {
    // When we are connected...
    if(wifiState == WIFI_CONNECTED) {
      // ---------------------------------------------------------------------------------------------------
      // TIME KEEPING
      // ---------------------------------------------------------------------------------------------------
      if(currentTime > ntpLastSync + NTP_TIMEOUT) {
        // Perform NTP time sync
        ntpLastSync = currentTime;
        setClock();
      }
      
      // ---------------------------------------------------------------------------------------------------
      // DATA RETRIEVAL
      // ----------------------------------------------------------------------------------------------------
      
      // Last measurement  more than 5 seconds ago?
      if(currentTime > dataLastMeasurementTime + MEASUREMENT_INTERVAL) {
        Serial.println ( "[DATA] - Collecting measurement data" );
        dataLastMeasurementTime = currentTime;

/*
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * Here, we perform the measurement and insert all the values in the JSON document to send to Azure IoT Hub
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 */
        if (! bme.performReading()) {
          Serial.println("-> ERROR: failed to perform reading of BME680 sensor!");
        } else {
          // Succesful update of sensor values; send them to the IoT Hub
          StaticJsonDocument<200> jsonBuffer;
  
          // Populate keys in json
          time_t currentTime = time(nullptr);
          jsonBuffer["DeviceId"] = String(iotDeviceId);
          jsonBuffer["MeasurementTime"] = String(currentTime);
          jsonBuffer["Temperature"] = bme.temperature;
          jsonBuffer["Pressure"] = bme.pressure / 100.0;
          jsonBuffer["Humidity"] = bme.humidity;
          jsonBuffer["Gas"] = bme.gas_resistance / 1000.0;
          jsonBuffer["Altitude"] = bme.readAltitude(SEALEVELPRESSURE_HPA);
                
          // convert json to buffer for publishing
          char buffer[256];
          uint16_t jsonSize = serializeJson(jsonBuffer, buffer, sizeof(buffer));

/*
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * To know what we are doing, we also dump the IoT payload to the console for debugging purposes
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 * **********************************************************************************************************************************
 */
          // Output to console
          Serial.print("[DATA] - Sending to IoT hub: "); Serial.println(buffer);
  
          // Transfer data to IoT Hub
          sendIoTTelemetry(buffer, jsonSize);
          
        }
      }
    }
  }
}

 /* ====================================================================================================
  * Helper - HTTP
  * ==================================================================================================== */

void sendIoTTelemetry(char* iotMessage, size_t iotMessageSize) {
  // We can only send data if we are connected, obviously
  if (wifiState == WIFI_CONNECTED) {
    HTTPClient https;
    https.begin(wifiClient, iotHubURL); //Specify the URL and certificate
    https.addHeader("Authorization", iotPassword);
    https.addHeader("Content-Type", "application/json");

    // Post the data to IoT Hub
    int httpCode = https.POST((uint8_t*)iotMessage, iotMessageSize);

    // Check the returning code
    if (httpCode > 0) { 
      String response = https.getString();
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_NO_CONTENT ) {
        Serial.printf("[HTTPS] POST ok (returned: %d) - ", httpCode);
        Serial.println(response);
      } else {
        Serial.printf("[HTTPS] POST failed, code: %d = %s -", httpCode, https.errorToString(httpCode).c_str());
        Serial.println(response);
      }
    } else {
      Serial.println("[HTTPS] Error on HTTP request");
    }
    //Free the resources
    https.end(); 
  }

}


 /* ====================================================================================================
  * Helper - WIFI
  * ==================================================================================================== */

// onConnected callback 
void onConnected(const WiFiEventStationModeConnected& event){
  Serial.println ( "WiFi - Connected to AP; getting IP..." );
  wifiState = WIFI_APFOUND;    
}

// onDisconnect callback
void onDisconnect(const WiFiEventStationModeDisconnected& event){
  if (wifiState != WIFI_CONNECTING) {
    Serial.println ( "WiFi - Disconnected!" );
    wifiState = WIFI_DISCONNECTED;
  }
}

// onGotIP callback
void onGotIP(const WiFiEventStationModeGotIP& event){
  // We are connected now
  wifiState = WIFI_CONNECTED;
  digitalWrite(WIFI_LED, 0 );
  
  Serial.print( "WiFi - Got IP address: " );
  Serial.println(WiFi.localIP());   
}
  

 /* ====================================================================================================
  * Helper - TIME
  * ==================================================================================================== */
  
// Set time via NTP, as required for x.509 validation
void setClock() {
  configTime(0, 0, "be.pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", 1);        // TZ_Europe_Brussels
  tzset();
  
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current UTC time  : ");
  Serial.print(asctime(&timeinfo));
  Serial.print("Current local time: ");
  Serial.print(ctime(&now));
}
