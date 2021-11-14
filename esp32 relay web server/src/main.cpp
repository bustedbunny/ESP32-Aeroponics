#include <Arduino.h>
#include <NTPClient.h>

#define WIFI_SSID "bunny-PC"
#define WIFI_PSK "11111111"

#include "cert.h"
#include "private_key.h"

/** Check if we have multiple cores */
#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

// We will use wifi
#include <WiFi.h>

// Inlcudes for setting up the server
#include <HTTPSServer.hpp>

// Define the certificate data for the server (Certificate and private key)
#include <SSLCert.hpp>

// Includes to define request handler callbacks
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>

// Required do define ResourceNodes
#include <ResourceNode.hpp>

#include <WiFiUdp.h>
#include "SPIFFS.h"
#include <FS.h>
#include <string>
#include <ArduinoJson.h>

// Easier access to the classes of the server
using namespace httpsserver;

SSLCert cert = SSLCert(
    example_crt_DER,     // DER-formatted certificate data
    example_crt_DER_len, // length of the certificate data
    example_key_DER,     // private key for that certificate
    example_key_DER_len  // Length of the private key
);

char contentTypes[][2][32] = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".png", "image/png"},
    {".jpg", "image/jpg"},
    {"", ""}};

unsigned long previousMillis = 0;
unsigned int interval = 30000;

bool a = 0;
bool b = 0;
bool c = 0;
bool upd = 1;
bool relay1, relay2, relay1mode = true, relay2mode = true;

int temp, humi, dimm;
char str[256];
char buffer[256];
char ConsoleBuffer[256];
StaticJsonDocument<256> doc;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600 + 1, 60000);
HTTPServer secureServer = HTTPServer();

void handleRoot(HTTPRequest *req, HTTPResponse *res);
void handle404(HTTPRequest *req, HTTPResponse *res);
void handleConsole(HTTPRequest *req, HTTPResponse *res);

// We declare a function that will be the entry-point for the task that is going to be
// created.
void serverTask(void *params);

void initWifi()
{
  Serial.println("Setting up WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }
  Serial.print("Connected. IP=");
  Serial.println(WiFi.localIP());
}

void setup()
{
  Serial.begin(115200);
  Serial2.begin(57600);
  // Initialize SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  initWifi();
  // Setup the server as a separate task.
  Serial.println("Creating server task... ");
  xTaskCreatePinnedToCore(serverTask, "server", 6144, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
  // NTP client initializing.
  timeClient.begin();
  pinMode(32, OUTPUT);
  pinMode(33, OUTPUT);


}

void serialEvent1(const char *buf)
{
  char writecheck[30];
  sscanf(buf, "%s", writecheck);
  if (strcmp(writecheck, "check") == 0)
  {
    sprintf(str, "relay1 = %d\nrelay2 = %d\nrelay1mode = %d\nrelay2mode = %d\n", relay1, relay2, relay1mode, relay2mode);
    Serial.print(str);
    // Serial2.print(str);
  }
  else if (strcmp(writecheck, "relay") == 0)
  {
    char var[30];
    uint8_t num, stat;
    sscanf(buf, "%s %d %s %d", writecheck, &num, var, &stat);
    if (num == 1)
    {
      if (strcmp(var, "Manual") == 0)
      {
        relay1mode = 0;
        relay1 = stat;
      }
      else
      {
        relay1mode = 1;
      }
    }
    if (num == 2)
    {
      if (strcmp(var, "Manual") == 0)
      {
        relay2mode = 0;
        relay2 = stat;
      }
      else
      {
        relay2mode = 1;
      }
    }
  }
  else if (strcmp(writecheck, "updateValues") == 0)
  {
    temp = random(255);
    humi = random(255);
    doc["temp"] = temp;
    doc["humi"] = humi;
    doc["r1"] = relay1;
    doc["r2"] = relay2;
    doc["dimm"] = dimm;
    doc["upd"] = upd;
    doc["millis"] = millis();
    serializeJson(doc, str);
    doc.clear();
  }
  else if (strcmp(writecheck, "upd") == 0)
  {
    doc["upd"] = upd;
    doc["millis"] = millis();
    if (relay1mode == 0)
    {
      doc["r1"] = relay1;
    }
    if (relay2mode == 0)
    {
      doc["r2"] = relay2;
    }
    doc["r1m"] = relay1mode;
    doc["r2m"] = relay2mode;
    serializeJson(doc, str);
    doc.clear();
  }
  else if (strcmp(writecheck, "toggleupdate") == 0)
  {
    upd = not upd;
    itoa(upd, str, 2);
  }
  else
  {
    Serial.print("My own echo: ");
    Serial.println(buf);
    Serial2.println(buf);
  }
}

void loop()
{
  timeClient.update();
  if (Serial.available())
  {
    size_t i = Serial.readBytesUntil('\n', buffer, 255);
    buffer[i] = '\0';
    serialEvent1(buffer);
    Serial2.println(buffer);
    buffer[0] = '\0';
  }
  if (Serial2.available())
  {
    size_t i = Serial2.readBytesUntil('\n', buffer, 255);
    buffer[i] = '\0';
    Serial.println(buffer);
    buffer[0] = '\0';
  }

  unsigned long currentMillis = millis();
  if (previousMillis > currentMillis)
  { //Обнуление счетчика
    previousMillis = 0;
  }

  if (currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    if (WiFi.status() != WL_CONNECTED)
    {
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }

  dimm = analogRead(35);

  if (relay1)
  {
    digitalWrite(32, HIGH);
  }
  else
  {
    digitalWrite(32, LOW);
  }

  if (relay2)
  {
    digitalWrite(33, HIGH);
  }
  else
  {
    digitalWrite(33, LOW);
  }
}

void serverTask(void *params)
{
  ResourceNode *nodeRoot = new ResourceNode("/", "GET", &handleRoot);
  ResourceNode *node404 = new ResourceNode("", "GET", &handle404);
  ResourceNode *nodeConsole = new ResourceNode("/console", "POST", &handleConsole);

  secureServer.registerNode(nodeRoot);
  secureServer.setDefaultNode(node404);
  secureServer.registerNode(nodeConsole);

  Serial.println("Starting server...");
  secureServer.start();
  if (secureServer.isRunning())
  {
    Serial.println("Server ready.");
  }
  while (true)
  {
    secureServer.loop();
    if (relay1mode)
    {
      if (timeClient.getHours() > 4 && timeClient.getHours() < 23)
      {
        relay1 = true;
      }

      else
      {
        relay1 = false;
      }
    }
    if (relay2mode)
    {
      if ((timeClient.getMinutes() >= 0 && timeClient.getMinutes() < 15) || (timeClient.getMinutes() >= 30 && timeClient.getMinutes() < 60))
      {
        relay2 = true;
      }
      else
      {
        relay2 = false;
      }
    }
  }
}

void handleRoot(HTTPRequest *req, HTTPResponse *res)
{
  if (req->getMethod() == "GET")
  {
    // Redirect / to /index.html
    std::string reqFile = req->getRequestString() == "/" ? "/index.html" : req->getRequestString();
    // Try to open the file
    std::string filename = reqFile;
    // Check if the file exists
    if (!SPIFFS.exists(filename.c_str()))
    {
      // Send "404 Not Found" as response, as the file doesn't seem to exist
      res->setStatusCode(404);
      res->setStatusText("Not found");
      res->println("404 Not Found");
      return;
    }
    File file = SPIFFS.open(filename.c_str());
    // Set length
    res->setHeader("Content-Length", httpsserver::intToString(file.size()));
    // Content-Type is guessed using the definition of the contentTypes-table defined above
    int cTypeIdx = 0;
    do
    {
      if (reqFile.rfind(contentTypes[cTypeIdx][0]) != std::string::npos)
      {
        res->setHeader("Content-Type", contentTypes[cTypeIdx][1]);
        break;
      }
      cTypeIdx += 1;
    } while (strlen(contentTypes[cTypeIdx][0]) > 0);

    // Read the file and write it to the response
    uint8_t buffer[256];
    size_t length = 0;
    do
    {
      length = file.read(buffer, 256);
      res->write(buffer, length);
    } while (length > 0);

    file.close();
  }
  else
  {
    // If there's any body, discard it
    req->discardRequestBody();
    // Send "405 Method not allowed" as response
    res->setStatusCode(405);
    res->setStatusText("Method not allowed");
    res->println("405 Method not allowed");
  }
}

void handleConsole(HTTPRequest *req, HTTPResponse *res)
{
  size_t s = req->readChars(ConsoleBuffer, 255);
  ConsoleBuffer[s] = '\0';
  serialEvent1(ConsoleBuffer);
  res->print(str);
  str[0] = '\0';
  ConsoleBuffer[0] = '\0';
}

void handle404(HTTPRequest *req, HTTPResponse *res)
{
  req->discardRequestBody();
  res->setStatusCode(404);
  res->setStatusText("Not Found");
  res->setHeader("Content-Type", "text/html");

  res->println("<!DOCTYPE html>");
  res->println("<html>");
  res->println("<head><title>Not Found</title></head>");
  res->println("<body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body>");
  res->println("</html>");
}
