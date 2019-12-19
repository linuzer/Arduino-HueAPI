#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <MQTT.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#include <FastLED.h>
#include <Arduino.h>
#include "Hue.h"

// physical length of led-strip
#define NUM_LEDS 277

// FastLED settings, data and clock pin for spi communication
// Note that the protocol for SM16716 is the same for the SM16726
#define DATA_PIN 22
#define CLOCK_PIN 19
#define COLOR_ORDER BGR
#define LED_TYPE SK9822
#define CORRECTION TypicalSMD5050

// May get messed up with SPI CLOCK_PIN with this particular bulb
#define use_hardware_switch false // To control on/off state and brightness using GPIO/Pushbutton, set this value to true.
//For GPIO based on/off and brightness control, it is mandatory to connect the following GPIO pins to ground using 10k resistor
#define onPin 4 // on and brightness up
#define offPin 5 // off and brightness down

// !!!!!!!! Experimental !!!!!!!!!!
// True - add cold white LEDs according to luminance/ whiteness in xy color selector
// False - Don't
#define W_ON_XY true

// Set up array for use by FastLED
CRGBArray<NUM_LEDS> leds;

// define details of virtual hue-lights -- adapt to your needs!
String HUE_Name = "Hue SK9822 FastLED strip";   //max 32 characters!!!
int HUE_LightsCount = 4;                     //number of emulated hue-lights
int HUE_PixelPerLight = 57;                  //number of leds forming one emulated hue-light
int HUE_TransitionLeds = 12;                 //number of 'space'-leds inbetween the emulated hue-lights; pixelCount must be divisible by this value
int HUE_FirstHueLightNr = 18;                //first free number for the first hue-light (look in diyHue config.json)
int HUE_ColorCorrectionRGB[3] = {100, 100, 100};  // light multiplier in percentage /R, G, B/

bool useDhcp = true;
IPAddress address ( 192,  168,   2,  95);     // choose an unique IP Adress
IPAddress gateway ( 192,  168,   2,   1);     // Router IP
IPAddress submask(255, 255, 255,   0);
byte mac[6]; // to hold  the wifi mac address

uint8_t scene;
uint8_t startup;
bool inTransition;
bool hwSwitch = false;

WebServer websrv(80);
Preferences Conf;
MQTTClient MQTT(1024);
WiFiClient net;

HueApi objHue = HueApi(leds, mac, HUE_FirstHueLightNr);
  
void setup() {

  Serial.begin(115200);
  Serial.println();
  delay(1000);
  
  Conf.begin("HueLED", false);
  
  FastLED.addLeds<LED_TYPE, DATA_PIN, CLOCK_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( CORRECTION );
   
  WiFiManager wifiManager;
  if (!useDhcp) {
    wifiManager.setSTAStaticIPConfig(address, gateway, submask);
  }
  wifiManager.autoConnect("New Hue Light");

  if (useDhcp) {
    address = WiFi.localIP();
    gateway = WiFi.gatewayIP();
    submask = WiFi.subnetMask();
  }

  infoLight(CRGB::White);
  while (WiFi.status() != WL_CONNECTED) {
    infoLight(CRGB::Red);
    delay(500);
  }
  // Show that we are connected
  infoLight(CRGB::Green);

  WiFi.macAddress(mac);         //gets the mac-address

  // ArduinoOTA.setPort(8266);                      // Port defaults to 8266
  // ArduinoOTA.setHostname("myesp8266");           // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setPassword((const char *)"123");   // No authentication by default
  ArduinoOTA.begin();

  if (use_hardware_switch == true) {
    pinMode(onPin, INPUT);
    pinMode(offPin, INPUT);
  }

  ConnectMQTT();
  
  if (loadConfig() == false) {saveConfig(); };
  loadConfig();
  restoreState();
  
  objHue.setupLights(HUE_Name, HUE_LightsCount, HUE_PixelPerLight, HUE_TransitionLeds);
  objHue.apply_scene(scene);
 
  websrv.on("/state", HTTP_PUT, websrvStatePut); 
  websrv.on("/state", HTTP_GET, websrvStateGet);
  websrv.on("/detect", websrvDetect);
  
  websrv.on("/config", websrvConfig);
  websrv.on("/", websrvRoot);
  websrv.on("/reset", websrvReset);
  websrv.onNotFound(websrvNotFound);

  websrv.on("/text_sensor/light_id", []() {
    websrv.send(200, "text/plain", "1");
  });
  
  websrv.begin();

  Log("Up and running.");
}

void loop() {
  ArduinoOTA.handle();
  websrv.handleClient();
  objHue.lightEngine();
  FastLED.show();

  EVERY_N_MILLISECONDS(200) {
    ArduinoOTA.handle();
    MQTT.loop();
  }
}


void ConnectMQTT(){

  Log("Connecting to MQTT-Broker: ");
  
  MQTT.begin("192.168.2.3", 1883, net);
  MQTT.setWill("iot/ledcontroller/log", "Off");
  
  while (!MQTT.connect("ledcontroller", "", "")) {
    Serial.print(".");
    delay(500);
  }

//  MQTT.onMessage(messageReceived);
  
  MQTT.subscribe("iot/ledcontroller/log");
/*  MQTT.subscribe(Conf.getString("ctrl.cmdtopic"));
  MQTT.subscribe(Conf.getString("ctrl.pcttopic"));
  MQTT.subscribe(Conf.getString("ctrl.rgbtopic"));
  MQTT.subscribe(Conf.getString("ctrl.scenetopic"));
  MQTT.subscribe(Conf.getString("ctrl.cfgtopic"));
*/
  Log("MQTT connected.\r\n");
}

void Log(String msg) {

  Serial.println(msg);

  if (MQTT.connected()) {
    MQTT.publish("iot/ledcontroller/log", msg);
  }
}

String WebLog(String message) {
  
  message += "URI: " + websrv.uri();
  message += "\r\n Method: " + (websrv.method() == HTTP_GET) ? "GET" : "POST";
  message += "\r\n Arguments: " + websrv.args(); + "\r\n";
  for (uint8_t i = 0; i < websrv.args(); i++) {
    message += " " + websrv.argName(i) + ": " + websrv.arg(i) + " \r\n";
  }
  
  Log(message);
  return message;
}

void infoLight(CRGB color) {
  
  // Flash the strip in the selected color. White = booted, green = WLAN connected, red = WLAN could not connect
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
    FastLED.show();
    leds.fadeToBlackBy(10);
  }
  leds = CRGB(CRGB::Black);
  FastLED.show();
}

void saveState() {
  
  String Output;
  DynamicJsonDocument json(1024);
  JsonObject light;
  
  for (uint8_t i = 0; i < HUE_LightsCount; i++) {
    light = json.createNestedObject((String)i);
    light["on"] = objHue.lights[i].lightState;
    light["bri"] = objHue.lights[i].bri;
    if (objHue.lights[i].colorMode == 1) {
      light["x"] = objHue.lights[i].x;
      light["y"] = objHue.lights[i].y;
    } else if (objHue.lights[i].colorMode == 2) {
      light["ct"] = objHue.lights[i].ct;
    } else if (objHue.lights[i].colorMode == 3) {
      light["hue"] = objHue.lights[i].hue;
      light["sat"] = objHue.lights[i].sat;
    }
  }
  
  serializeJson(json, Output);
  Conf.putString("StateJson", Output);
}

void restoreState() {
  
  String Input;
  DynamicJsonDocument json(1024);
  DeserializationError error;
  JsonObject values;
  const char* key;
  int lightId;
  
  Input = Conf.getString("StateJson");

  error = deserializeJson(json, Input);
  if (error) {
    //Serial.println("Failed to parse config file");
    return;
  }
  for (JsonPair state : json.as<JsonObject>()) {
    key = state.key().c_str();
    lightId = atoi(key);
    values = state.value();
    objHue.lights[lightId].lightState = values["on"];
    objHue.lights[lightId].bri = (uint8_t)values["bri"];
    if (values.containsKey("x")) {
      objHue.lights[lightId].x = values["x"];
      objHue.lights[lightId].y = values["y"];
      objHue.lights[lightId].colorMode = 1;
    } else if (values.containsKey("ct")) {
      objHue.lights[lightId].ct = values["ct"];
      objHue.lights[lightId].colorMode = 2;
    } else {
      if (values.containsKey("hue")) {
        objHue.lights[lightId].hue = values["hue"];
        objHue.lights[lightId].colorMode = 3;
      }
      if (values.containsKey("sat")) {
        objHue.lights[lightId].sat = (uint8_t) values["sat"];
        objHue.lights[lightId].colorMode = 3;
      }
      objHue.lights[lightId].color = CHSV(objHue.lights[lightId].hue, objHue.lights[lightId].sat, objHue.lights[lightId].bri);
    }
    objHue.processLightdata(lightId, 40);
  }
}

void saveConfig() {
  
  String Output;
  DynamicJsonDocument json(1024);
  JsonArray addr, gw, mask;

  json["name"] = HUE_Name;
  json["startup"] = startup;
  json["scene"] = scene;
  json["on"] = onPin;
  json["off"] = offPin;
  json["hw"] = hwSwitch;
  json["dhcp"] = useDhcp;
  json["lightsCount"] = HUE_LightsCount;
  json["pixelCount"] = HUE_PixelPerLight;
  json["transLeds"] = HUE_TransitionLeds;
  json["rpct"] = HUE_ColorCorrectionRGB[0];
  json["gpct"] = HUE_ColorCorrectionRGB[1];
  json["bpct"] = HUE_ColorCorrectionRGB[2];
  addr = json.createNestedArray("addr");
  addr.add(address[0]);
  addr.add(address[1]);
  addr.add(address[2]);
  addr.add(address[3]);
  gw = json.createNestedArray("gw");
  gw.add(gateway[0]);
  gw.add(gateway[1]);
  gw.add(gateway[2]);
  gw.add(gateway[3]);
  mask = json.createNestedArray("mask");
  mask.add(submask[0]);
  mask.add(submask[1]);
  mask.add(submask[2]);
  mask.add(submask[3]);

  serializeJson(json, Output);
  Conf.putString("ConfJson", Output);
  
  Log("saveConfig: " + Output);
  
}

bool loadConfig() {
  
  String Input;
  DynamicJsonDocument json(1024);
  DeserializationError error;
  
  Input = Conf.getString("ConfJson");

  Log("loadConfig: " + Input);
  
  error = deserializeJson(json, Input);
  if (error) {
    //Serial.println("Failed to parse config file");
    return false;
  } 

  HUE_Name = json["name"].as<String>();
  startup = json["startup"].as<uint8_t>();
  scene  = json["scene"].as<uint8_t>();
  HUE_LightsCount = json["lightsCount"].as<uint16_t>();
  HUE_PixelPerLight = json["pixelCount"].as<uint16_t>();
  HUE_TransitionLeds = json["transLeds"].as<uint8_t>();

  if (json.containsKey("rpct")) {
    HUE_ColorCorrectionRGB[0] = json["rpct"].as<uint8_t>();
    HUE_ColorCorrectionRGB[1] = json["gpct"].as<uint8_t>();
    HUE_ColorCorrectionRGB[2] = json["bpct"].as<uint8_t>();
  }
  useDhcp = json["dhcp"];
  address = {json["addr"][0], json["addr"][1], json["addr"][2], json["addr"][3]};
  submask = {json["mask"][0], json["mask"][1], json["mask"][2], json["mask"][3]};
  gateway = {json["gw"][0], json["gw"][1], json["gw"][2], json["gw"][3]};
  
  objHue.setupLights(HUE_Name, HUE_LightsCount, HUE_PixelPerLight, HUE_TransitionLeds);
  
  return true;
}

void websrvDetect() {
  String output = objHue.Detect();
  websrv.send(200, "text/plain", output);    
  Log("Detect: " + output);
}

void websrvStateGet() {
  String output = objHue.StateGet(websrv.arg("light"));
  websrv.send(200, "text/plain", output);
  Log("StateGet: " + output);
}

void websrvStatePut() {
  String output = objHue.StatePut(websrv.arg("plain"));
  if (output.substring(0, 4) == "FAIL") {
    websrv.send(404, "text/plain", "FAIL. " + websrv.arg("plain"));
    Log("StatePut: FAIL " + websrv.arg("plain"));
  }
  websrv.send(200, "text/plain", output);
  Log("StatePut: " + output);
}

void websrvReset() {
  websrv.send(200, "text/html", "reset");
  Log("Restart");
  delay(1000);
  esp_restart();
}

void websrvRoot() {

  Log("StateRoot: " + websrv.uri());
  
  if (websrv.arg("section").toInt() == 1) {
    HUE_Name = websrv.arg("name");
    startup = websrv.arg("startup").toInt();
    scene = websrv.arg("scene").toInt();
    HUE_LightsCount = websrv.arg("lightscount").toInt();
    HUE_PixelPerLight = websrv.arg("pixelcount").toInt();
    HUE_TransitionLeds = websrv.arg("transitionleds").toInt();
    HUE_ColorCorrectionRGB[0] = websrv.arg("rpct").toInt();
    HUE_ColorCorrectionRGB[1] = websrv.arg("gpct").toInt();
    HUE_ColorCorrectionRGB[2] = websrv.arg("bpct").toInt();
    hwSwitch = websrv.hasArg("hwswitch") ? websrv.arg("hwswitch").toInt() : 0;
    
    saveConfig();
  } else if (websrv.arg("section").toInt() == 2) {
    useDhcp = (!websrv.hasArg("disdhcp")) ? 1 : websrv.arg("disdhcp").toInt();
    if (websrv.hasArg("disdhcp")) {
      address.fromString(websrv.arg("addr"));
      gateway.fromString(websrv.arg("gw"));
      submask.fromString(websrv.arg("sm"));
    }
    saveConfig();
  }

  String htmlContent = "<!DOCTYPE html> <html> <head> <meta charset=\"UTF-8\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"> <title>Hue Light</title> <link href=\"https://fonts.googleapis.com/icon?family=Material+Icons\" rel=\"stylesheet\"> <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css\"> <link rel=\"stylesheet\" href=\"https://cerny.in/nouislider.css\"/> </head> <body> <div class=\"wrapper\"> <nav class=\"nav-extended row deep-purple\"> <div class=\"nav-wrapper col s12\"> <a href=\"#\" class=\"brand-logo\">DiyHue</a> <ul id=\"nav-mobile\" class=\"right hide-on-med-and-down\" style=\"position: relative;z-index: 10;\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\"><i class=\"material-icons left\">language</i>GitHub</a></li> <li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\"><i class=\"material-icons left\">description</i>Documentation</a></li> <li><a target=\"_blank\" href=\"https://diyhue.slack.com/\" ><i class=\"material-icons left\">question_answer</i>Slack channel</a></li> </ul> </div> <div class=\"nav-content\"> <ul class=\"tabs tabs-transparent\"> <li class=\"tab\"><a class=\"active\" href=\"#test1\">Home</a></li> <li class=\"tab\"><a href=\"#test2\">Preferences</a></li> <li class=\"tab\"><a href=\"#test3\">Network settings</a></li> </ul> </div> </nav> <ul class=\"sidenav\" id=\"mobile-demo\"> <li><a target=\"_blank\" href=\"https://github.com/diyhue\">GitHub</a></li> <li><a target=\"_blank\" href=\"https://diyhue.readthedocs.io/en/latest/\">Documentation</a></li> <li><a target=\"_blank\" href=\"https://diyhue.slack.com/\" >Slack channel</a></li> </ul> <div class=\"container\"> <div class=\"section\"> <div id=\"test1\" class=\"col s12\"> <form> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s10\"> <label for=\"power\">Power</label> <div id=\"power\" class=\"switch section\"> <label> Off <input type=\"checkbox\" name=\"pow\" id=\"pow\" value=\"1\"> <span class=\"lever\"></span> On </label> </div> </div> </div> <div class=\"row\"> <div class=\"col s12 m10\"> <label for=\"bri\">Brightness</label> <input type=\"text\" id=\"bri\" class=\"js-range-slider\" name=\"bri\" value=\"\"/> </div> </div> <div class=\"row\"> <div class=\"col s12\"> <label for=\"hue\">Color</label> <div> <canvas id=\"hue\" width=\"320px\" height=\"320px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </div> <div class=\"row\"> <div class=\"col s12\"> <label for=\"ct\">Color Temp</label> <div> <canvas id=\"ct\" width=\"320px\" height=\"50px\" style=\"border:1px solid #d3d3d3;\"></canvas> </div> </div> </div> </form> </div> <div id=\"test2\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"1\"> <div class=\"row\"> <div class=\"col s12\"> <label for=\"name\">Light Name</label> <input type=\"text\" id=\"name\" name=\"name\"> </div> </div> <div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"startup\">Default Power:</label> <select name=\"startup\" id=\"startup\"> <option value=\"0\">Last State</option> <option value=\"1\">On</option> <option value=\"2\">Off</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s12 m6\"> <label for=\"scene\">Default Scene:</label> <select name=\"scene\" id=\"scene\"> <option value=\"0\">Relax</option> <option value=\"1\">Read</option> <option value=\"2\">Concentrate</option> <option value=\"3\">Energize</option> <option value=\"4\">Bright</option> <option value=\"5\">Dimmed</option> <option value=\"6\">Nightlight</option> <option value=\"7\">Savanna sunset</option> <option value=\"8\">Tropical twilight</option> <option value=\"9\">Arctic aurora</option> <option value=\"10\">Spring blossom</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"pixelcount\" class=\"col-form-label\">Pixel count</label> <input type=\"number\" id=\"pixelcount\" name=\"pixelcount\"> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"lightscount\" class=\"col-form-label\">Lights count</label> <input type=\"number\" id=\"lightscount\" name=\"lightscount\"> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"transitionleds\">Transition leds:</label> <select name=\"transitionleds\" id=\"transitionleds\"> <option value=\"0\">0</option> <option value=\"2\">2</option> <option value=\"4\">4</option> <option value=\"6\">6</option> <option value=\"8\">8</option> <option value=\"10\">10</option> <option value=\"12\">12</option> <option value=\"14\">14</option> <option value=\"16\">16</option> <option value=\"18\">18</option> <option value=\"20\">20</option> </select> </div> </div> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"rpct\" class=\"form-label\">Red multiplier</label> <input type=\"number\" id=\"rpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"rpct\" value=\"\"/> </div> <div class=\"col s4 m3\"> <label for=\"gpct\" class=\"form-label\">Green multiplier</label> <input type=\"number\" id=\"gpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"gpct\" value=\"\"/> </div> <div class=\"col s4 m3\"> <label for=\"bpct\" class=\"form-label\">Blue multiplier</label> <input type=\"number\" id=\"bpct\" class=\"js-range-slider\" data-skin=\"round\" name=\"bpct\" value=\"\"/> </div> </div> <div class=\"row\"><label class=\"control-label col s10\">HW buttons:</label> <div class=\"col s10\"> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"hwswitch\" id=\"hwswitch\" value=\"1\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"> <div class=\"row\"> <div class=\"col s4 m3\"> <label for=\"on\">On Pin</label> <input type=\"number\" id=\"on\" name=\"on\"> </div> <div class=\"col s4 m3\"> <label for=\"off\">Off Pin</label> <input type=\"number\" id=\"off\" name=\"off\"> </div> </div> </div> <div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> </div> </div> </form> </div> <div id=\"test3\" class=\"col s12\"> <form method=\"POST\" action=\"/\"> <input type=\"hidden\" name=\"section\" value=\"2\"> <div class=\"row\"> <div class=\"col s12\"> <label class=\"control-label\">Manual IP assignment:</label> <div class=\"switch section\"> <label> Disable <input type=\"checkbox\" name=\"disdhcp\" id=\"disdhcp\" value=\"0\"> <span class=\"lever\"></span> Enable </label> </div> </div> </div> <div class=\"switchable\"> <div class=\"row\"> <div class=\"col s12 m3\"> <label for=\"addr\">Ip</label> <input type=\"text\" id=\"addr\" name=\"addr\"> </div> <div class=\"col s12 m3\"> <label for=\"sm\">Submask</label> <input type=\"text\" id=\"sm\" name=\"sm\"> </div> <div class=\"col s12 m3\"> <label for=\"gw\">Gateway</label> <input type=\"text\" id=\"gw\" name=\"gw\"> </div> </div> </div> <div class=\"row\"> <div class=\"col s10\"> <button type=\"submit\" class=\"waves-effect waves-light btn teal\">Save</button> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> <!--<button type=\"submit\" name=\"reboot\" class=\"waves-effect waves-light btn grey lighten-1\">Reboot</button>--> </div> </div> </form> </div> </div> </div> </div> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/jquery/3.4.1/jquery.min.js\"></script> <script src=\"https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js\"></script> <script src=\"https://cerny.in/nouislider.js\"></script> <script src=\"https://cerny.in/diyhue.js\"></script> </body> </html>";

  websrv.send(200, "text/html", htmlContent);
  if (websrv.args()) {
    websrvReset();
  }
}

void websrvConfig() {
  
  DynamicJsonDocument root(1024);
  String output;
  HueApi::state tmpState;
  
  root["name"] = HUE_Name;
  root["scene"] = scene;
  root["startup"] = startup;
  root["hw"] = hwSwitch;
  root["on"] = onPin;
  root["off"] = offPin;
  root["hwswitch"] = (int)hwSwitch;
  root["lightscount"] = HUE_LightsCount;
  root["pixelcount"] = HUE_PixelPerLight;
  root["transitionleds"] = HUE_TransitionLeds;
  root["rpct"] = RGB_R;
  root["gpct"] = RGB_G;
  root["bpct"] = RGB_B;
  root["disdhcp"] = (int)!useDhcp;
  root["addr"] = (String)address[0] + "." + (String)address[1] + "." + (String)address[2] + "." + (String)address[3];
  root["gw"] = (String)gateway[0] + "." + (String)gateway[1] + "." + (String)gateway[2] + "." + (String)gateway[3];
  root["sm"] = (String)submask[0] + "." + (String)submask[1] + "." + (String)submask[2] + "." + (String)submask[3];
  
  serializeJson(root, output);
  websrv.send(200, "text/plain", output);
  Log("Config: " + output);
}

void websrvNotFound() {
  websrv.send(404, "text/plain", WebLog("File Not Found\n\n"));
}


/*void convert_hue() {
  double      hh, p, q, t, ff, s, v;
  long        i;


  s = sat / 255.0;
  v = bri / 255.0;

  // Test for intensity for white LEDs
  float I = (float)(sat + bri) / 2;

  if (s <= 0.0) {      // < is bogus, just shuts up warnings
    rgbw[0] = v;
    rgbw[1] = v;
    rgbw[2] = v;
    return;
  }
  hh = hue;
  if (hh >= 65535.0) hh = 0.0;
  hh /= 11850, 0;
  i = (long)hh;
  ff = hh - i;
  p = v * (1.0 - s);
  q = v * (1.0 - (s * ff));
  t = v * (1.0 - (s * (1.0 - ff)));

  switch (i) {
    case 0:
      rgbw[0] = v * 255.0;
      rgbw[1] = t * 255.0;
      rgbw[2] = p * 255.0;
      rgbw[3] = I;
      break;
    case 1:
      rgbw[0] = q * 255.0;
      rgbw[1] = v * 255.0;
      rgbw[2] = p * 255.0;
      rgbw[3] = I;
      break;
    case 2:
      rgbw[0] = p * 255.0;
      rgbw[1] = v * 255.0;
      rgbw[2] = t * 255.0;
      rgbw[3] = I;
      break;

    case 3:
      rgbw[0] = p * 255.0;
      rgbw[1] = q * 255.0;
      rgbw[2] = v * 255.0;
      rgbw[3] = I;
      break;
    case 4:
      rgbw[0] = t * 255.0;
      rgbw[1] = p * 255.0;
      rgbw[2] = v * 255.0;
      rgbw[3] = I;
      break;
    case 5:
    default:
      rgbw[0] = v * 255.0;
      rgbw[1] = p * 255.0;
      rgbw[2] = q * 255.0;
      rgbw[3] = I;
      break;
  }

} */
