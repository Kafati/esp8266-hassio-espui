//import all libraries

#include "PCF8574.h"
#include <DNSServer.h>
#include <ESPUI.h>
#include <EEPROM.h>
#include <MQTT.h>                 //https://github.com/256dpi/arduino-mqtt
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson/releases/download/v6.8.0-beta/ArduinoJson-v6.8.0-beta.zip
#include <PCF8574.h>              //https://github.com/xreef/PCF8574_library
#include <Ticker.h>


// constants
#define DEVICES 8
#define SDA_PIN 4 //D2 on NodeMCU
#define SCL_PIN 5 //D1 on NodeMCU
#define BUTTONS_ADDRESS 0x20
#define RELAYS_ADDRESS 0x21
#define HOSTNAME "PowerHouse"

uint16_t UIssid;
uint16_t UIpassword;
uint16_t UIsaveButton;
uint16_t UIrestartButton;
uint16_t UIMqttServer;
uint16_t UIusername;
uint16_t UImqpassword;
unsigned long UI_sw_startTime;



//ESPUI
#define WIFI_CRED_LENGTH 21 



//Variables 
bool keyPressed = false;
bool lastButtonStates[DEVICES];
bool shouldUpdateLights = false;
bool wifiConnected = false;
bool UI_sw_eepromset;
char UI_ssid_ca[WIFI_CRED_LENGTH] = "sensorConfig";
char WN_ssid_ca[WIFI_CRED_LENGTH] = {};         //   -setup SSID for the local Wifi network. leave empty for web interface setup!
char WN_password_ca[WIFI_CRED_LENGTH] = {};     //   -setup password
char mqtt_port[6] = "1883";
char mqtt_client_name[100]= HOSTNAME ;
char light_topic_in[100] = "";
char light_topic_out[100] = "";
  byte mac[6];

const char * charPtr;
const char *UI_password = "maxfactor";
const char *WN_ssid = WN_ssid_ca;
const char *UI_ssid = UI_ssid_ca;
const char *WN_password = WN_password_ca;
const byte wifiTimeout = 30;                    // @EB-custom timeout in seconds
const byte UI_DNS_PORT = 53;
const byte UI_sw_length = WIFI_CRED_LENGTH;    
const int eepromBufSize = 1024;   
const int maxNumOfSSIDs = 20;   
const unsigned int wifiSetupTimeout = 300;
String ssids[maxNumOfSSIDs];
int numOfssids;

// Created data structures

struct LED_LIGHTS
{
  uint8_t pin;
  bool state = false;
  uint16_t identity ;
} Relay[DEVICES];


struct UI_sw_settings_ST {
  char idStart = ' ';
  char ssid[UI_sw_length];
  char password[UI_sw_length];
  char server[UI_sw_length];
  char username[UI_sw_length];
  char mqpassword[UI_sw_length];
  
  char idEnd = ' ';

}Configs;



// Constructors
PCF8574 relays(RELAYS_ADDRESS, SDA_PIN, SCL_PIN);
PCF8574 buttons(BUTTONS_ADDRESS, SDA_PIN, SCL_PIN);
DNSServer UI_dnsServer;
IPAddress lanIP;
IPAddress UI_apIP(192, 168, 4, 2);
DNSServer dnsServer;
WiFiClient net;
MQTTClient client(512);
Ticker sendStat;



//Functions

void keyPressedOnPCF8574(){
  // Interrupt called (No Serial no read no wire in this function, and DEBUG disabled on PCF library)
   keyPressed = true;

}


//initialise pins as inputs
void initButtons(void)
{
  for (uint8_t i = 0; i < DEVICES; i++)
  {
    buttons.pinMode(i, INPUT_PULLUP);
  }
  buttons.begin();

}

void initRelays(void)
{
  for (uint8_t i = 0; i < DEVICES; i++)
  {
    Relay[i].pin = i;
    relays.pinMode(Relay[i].pin, OUTPUT);
  }
  relays.begin();
}

void setRelays(uint8_t single_pin = 99)
{
  uint8_t begin_i = 0, end_i = DEVICES;
  if (single_pin != 99)
  {
    begin_i = single_pin;
    end_i = single_pin + 1;
  }
  for (uint8_t i = begin_i; i < end_i; i++){
    relays.digitalWrite(Relay[i].pin, (Relay[i].state) ? LOW : HIGH); // active LOW is relay ON
         ESPUI.updateSwitcher(Relay[i].identity, Relay[i].state);
}
 shouldUpdateLights = false;

}
bool changed(bool a, bool b) {
  if (a != b){ return HIGH;}else{return LOW;}
  }

void readAll() {

    for (uint8_t i = 0; i < DEVICES; i++){
if (changed(digitalRead(i),lastButtonStates[i])) 
(  (Relay[i].state == LOW)? Relay[i].state =HIGH : Relay[i].state == LOW );
 
   
  }
}

void setAllOn(void)
{
  for (uint8_t i = 0; i < DEVICES; i++)
    Relay[i].state = true;
  setRelays();
}

void setAllOff(void)
{
  for (uint8_t i = 0; i < DEVICES; i++)
    Relay[i].state = false;
  setRelays();
}

void writeEEPROM() {  
  EEPROM.begin(eepromBufSize);
  EEPROM.put(0, Configs);
  EEPROM.commit();

  Serial.println("EEPROM written:,\n SSID: \t" + (String) Configs.ssid);
  Serial.println(" password \t: " + (String) Configs.password);
  Serial.println(" server\t: " + (String) Configs.server);
  Serial.println(" Username \t: " + (String) Configs.username);
  Serial.println(" MQTT password \t: " + (String) Configs.mqpassword);

  

}



void resetEEPROM() {
  Configs.idStart = ' ';
  for (int i = 0; i < UI_sw_length; i++) {
 Configs.ssid[i] = 0;
 Configs.password[i] = 0;
 Configs.server[i] = 0;
 Configs.username[i] = 0;
 Configs.mqpassword[i] = 0;
    
  }
  Configs.idEnd = ' ';
  writeEEPROM();
}

bool readEEPROM() {
  EEPROM.begin(sizeof(Configs));
  EEPROM.get(0, Configs);

  if (Configs.idStart == '$' && Configs.idEnd == '*') {
    return true;
  } else {
    Serial.println("Wifi setup EEPROM identifiers failed " + (String) Configs.idStart + " " + (String) Configs.idEnd + ", initializing");
    resetEEPROM();
    return false;
  }
}


bool setupWifiConnection() {
  unsigned long wifiStartTime = millis();
  bool connectStat = true;
 
    Serial.println("Connecting to Wifi ");

  #ifndef NO_WIFI
    WiFi.mode(WIFI_AP_STA);                 

    #ifdef ESP32
      WiFi.setHostname(UI_ssid_ca);
    #else
      WiFi.hostname(UI_ssid_ca);
    #endif
      Serial.println("Hostname set to ");
      Serial.println(UI_ssid);

    WiFi.begin(WN_ssid, WN_password);
  
    while (WiFi.status() != WL_CONNECTED && ((millis() - wifiStartTime) < (wifiTimeout * 1000))) {
      connectStat = !connectStat;
      Serial.print(">");
      delay(500);
    }
  
    if (WiFi.status() == WL_CONNECTED) {  
      wifiConnected = true;
      lanIP = WiFi.localIP();
        Serial.print("connected to Wifi at ");
        Serial.println(lanIP);
        WiFi.macAddress(mac);
  sprintf(mqtt_client_name, "%02X%02X%02X%02X%02X%02X%s", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], HOSTNAME);
  sprintf(light_topic_in, "home/%02X%02X%02X%02X%02X%02X%s", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], "/in");
  sprintf(light_topic_out, "home/%02X%02X%02X%02X%02X%02X%s", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], "/out");
       return true;
      
    
      
    } else {    
        Serial.println("Failed to connect to wifi");
        return false;
    }
  #endif
}


String fillAsterisk(String value, byte valLength) {
  String tmpStr = "";
  while (tmpStr.length() < value.length())
    tmpStr += "*"; 
  return String(tmpStr);
}

void UI_sw_updatePassword() {
  UI_sw_startTime = millis();
  ESPUI.updateText(UIpassword, fillAsterisk(Configs.password, (UI_sw_length) - 1));
}


bool configureWIFI() {
  bool connectedWifi;
  if (WN_ssid_ca[0] != 0)  {           // if ssid is set, try to connect
        connectedWifi =setupWifiConnection();}
        else{
        if (!wifiConnected) {             // wifi not connected so read the eeprom for ssid and password settings
          UI_sw_eepromset = readEEPROM();
    
          if (UI_sw_eepromset) {          // data read so try to connect
              Serial.print("SSID and password read from EEPROM. SSID: ");
              Serial.print(Configs.ssid);
              Serial.print(" password: ");
              Serial.print(Configs.password);
              Serial.println("");

            strcpy(WN_ssid_ca, Configs.ssid);
            strcpy(WN_password_ca, Configs.password);
            connectedWifi= setupWifiConnection();
          } else {
              Serial.println("SSID and password not read from EEPROM");
          }
        }
        }
        return connectedWifi;
  }

  void setupHotspot(){


  WiFi.mode(WIFI_AP);                     //   -todo
  WiFi.softAPConfig(UI_apIP, UI_apIP, IPAddress(255, 255, 255, 0));

    WiFi.hostname(UI_ssid_ca);

  

  WiFi.softAP(UI_ssid, UI_password);
  Serial.print("Access point started");


    
    }


  void  scanSSIDs(){



 Serial.println("Scanning for networks...");
  numOfssids = 0;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  int foundSSIDs = WiFi.scanNetworks();
  Serial.println("found " + (String) foundSSIDs + " networks");

  if (foundSSIDs) {
    for (int i = 0; i < foundSSIDs; ++i) {

//      if ((i < maxNumOfSSIDs) && (WiFi.SSID(i).length() < WIFI_CRED_LENGTH)) { //   -todo. NOTE: if skipping ssids, ssids[i] won't match anymore!!

      if ((i < maxNumOfSSIDs)) {
        numOfssids++;
        ssids[numOfssids - 1] = WiFi.SSID(i);
      
//      Serial.print(WiFi.RSSI(i));
//      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      }
    }
  }


      
      }


 void callText(Control *sender, int type) {
  UI_sw_startTime = millis();
  if (sender->id == UIpassword) {
    sender->value.toCharArray(Configs.password, UI_sw_length);
    UI_sw_updatePassword();
  }
    else if (sender->id == UIMqttServer) {
    sender->value.toCharArray(Configs.server, UI_sw_length);
// /   UI_sw_updatePassword();
  }
  else if (sender->id == UIusername) {
    sender->value.toCharArray(Configs.username, UI_sw_length);
//   / UI_sw_updatePassword();
  }
    else if (sender->id == UImqpassword) {
    sender->value.toCharArray(Configs.mqpassword, UI_sw_length);
// /   UI_sw_updatePassword();
  }
  
   

}


void switchExample(Control* sender, int value)
{  
  for (int i = 0; i < DEVICES; i++) {
      if (Relay[i].identity == sender->id) {
    switch (value)
    {
    case S_ACTIVE:
        Relay[i].state = true;
        break;

    case S_INACTIVE:
        Relay[i].state = false;
        break;
    }

    Serial.print(" ");
    Serial.println(sender->id);
    break;
      }
}
shouldUpdateLights = true;
}



void callButton(Control *sender, int type) {
  UI_sw_startTime = millis();
  if (type == B_UP) {
    if (sender->id == UIsaveButton) {
      Configs.idStart = '$';
      Configs.idEnd = '*';
      writeEEPROM();

      ESP.restart();
    } else if (sender->id == UIrestartButton) {
      ESP.restart();
    }
  }
}

void callSelect(Control *sender, int type) {
  UI_sw_startTime = millis();
  if (sender->id == UIssid) {
    int value = sender->value.toInt();
    ssids[value].toCharArray(Configs.ssid, UI_sw_length);
  }
}

 void createGui () {
    scanSSIDs();

        uint16_t tab1 = ESPUI.addControl(ControlType::Tab, "Use", "Settings 1");    
        uint16_t tab2 = ESPUI.addControl(ControlType::Tab, "Configure", "Settings 1");
  UI_dnsServer.start (UI_DNS_PORT, "*", UI_apIP);
 Serial.println("DNS server started");
 UIssid = ESPUI.addControl(ControlType::Select, "SSID", "", ControlColor::Peterriver, Control::noParent, &callSelect);
  for (int i = 0; i < numOfssids; i++) {
    charPtr = ssids[i].c_str();
    ESPUI.addControl(ControlType::Option, charPtr, (String) i, ControlColor::Peterriver, UIssid);
  }

  if (numOfssids)   // set ssid to first selection
    ssids[0].toCharArray(Configs.ssid, UI_sw_length);
  UIpassword = ESPUI.addControl(ControlType::Text, "Password", Configs.password, ControlColor::Peterriver, tab2, &callText);
  UIMqttServer = ESPUI.addControl(ControlType::Text, "Server", Configs.server, ControlColor::Peterriver, tab2, &callText);
  UIusername = ESPUI.addControl(ControlType::Text, "Username", Configs.username, ControlColor::Peterriver, tab2, &callText);
  UImqpassword = ESPUI.addControl(ControlType::Text, "Server password", Configs.mqpassword, ControlColor::Peterriver, tab2, &callText);
  UIsaveButton = ESPUI.addControl(ControlType::Button, "Save", "Save", ControlColor::Alizarin, tab2, &callButton);
  UIrestartButton = ESPUI.addControl(ControlType::Button, "Restart", "Restart", ControlColor::Alizarin, tab2, &callButton);
  
   String prefix = "Switch ";
     for (int i = 0; i < DEVICES; i++) {
       String result = prefix + i;
    Relay[i].identity = ESPUI.addControl(ControlType::Switcher, result.c_str(), "", ControlColor::Alizarin, tab1, &switchExample);

  }
   
   
   
   ESPUI.begin("Netswitch");


  
  
  }


String statusMsg(void)
{
  /*
  Will send out something like this:
  {
    "relay1":"OFF",
    "relay2":"OFF",
    "relay3":"OFF",
    "relay4":"OFF",
    "relay5":"OFF",
    "relay6":"OFF",
    "relay7":"OFF",
    "relay8":"OFF"
  }
  */

  DynamicJsonDocument json(JSON_OBJECT_SIZE(DEVICES) + 100);
  for (uint8_t i = 0; i < DEVICES; i++)
  {
    String l_name = "relay" + String(i + 1);
    json[l_name] = (Relay[i].state) ? "ON" : "OFF";
  }
  String msg_str;
  serializeJson(json, msg_str);
  return msg_str;
}

void sendMQTTStatusMsg(void)
{
  Serial.print(F("Sending ["));
  Serial.print(light_topic_out);
  Serial.print(F("] >> "));
  Serial.println(statusMsg());
  client.publish(light_topic_out, statusMsg());
  sendStat.detach();
}




void processJson(String &payload)
{
  /*
  incoming message template:
  {
    "relay": 1,
    "state": "ON"
  }
  */

  DynamicJsonDocument jsonBuffer(JSON_OBJECT_SIZE(2) + 100);
  DeserializationError error = deserializeJson(jsonBuffer, payload);
  if (error)
  {
    Serial.print(F("parseObject() failed: "));
    Serial.println(error.c_str());
  }
  JsonObject root = jsonBuffer.as<JsonObject>();

  if (root.containsKey("relay"))
  {
    uint8_t index = jsonBuffer["relay"];
    index--;
    if (index >= DEVICES)
      return;
    String stateValue = jsonBuffer["state"];
    if (stateValue == "ON" or stateValue == "on")
    {
      Relay[index].state = true;
      shouldUpdateLights = true;
      sendMQTTStatusMsg();
    }
    else if (stateValue == "OFF" or stateValue == "off")
    {
      Relay[index].state = false;
      shouldUpdateLights = true;
      sendMQTTStatusMsg();

    }
  }
}





  
void messageReceived(String &topic, String &payload)
{
  Serial.println("Incoming: [" + topic + "] << " + payload);
  processJson(payload);
}





void sendAutoDiscoverySwitch(String index, String &discovery_topic)
{
  /*
  "discovery topic" >> "homeassistant/switch/XXXXXXXXXXXXXXXX/config"

  Sending data that looks like this >>
  {
    "name":"relay1",
    "state_topic": "home/aabbccddeeff/out",
    "command_topic": "home/aabbccddeeff/in",
    "payload_on":"{'relay':1,'state':'ON'}",
    "payload_off":"{'relay':1,'state':'OFF'}",
    "value_template": "{{ value_json.relay1.value }}",
    "state_on": "ON",
    "state_off": "OFF",
    "optimistic": false,
    "qos": 0,
    "retain": true
  }
  */

  const size_t capacity = JSON_OBJECT_SIZE(11) + 500;
  DynamicJsonDocument json(capacity);

  json["name"] = String(HOSTNAME) + " " + index;
  json["state_topic"] = light_topic_out;
  json["command_topic"] = light_topic_in;
  json["payload_on"] = "{'relay':" + index + ",'state':'ON'}";
  json["payload_off"] = "{'relay':" + index + ",'state':'OFF'}";
  json["value_template"] = "{{value_json.relay" + index + "}}";
  json["state_on"] = "ON";
  json["state_off"] = "OFF";
  json["optimistic"] = false;
  json["qos"] = 0;
  json["retain"] = true;

  String msg_str;
  Serial.print(F("Sending AD MQTT ["));
  Serial.print(discovery_topic);
  Serial.print(F("] >> "));
  serializeJson(json, Serial);
  serializeJson(json, msg_str);
  client.publish(discovery_topic, msg_str, true, 0);
  Serial.println();
}

void sendAutoDiscovery(void)
{
  for (uint8_t i = 0; i < DEVICES; i++)
  {
    String dt = "homeassistant/switch/" + String(HOSTNAME) + String(i + 1) + "/config";
    sendAutoDiscoverySwitch(String(i + 1), dt);
  }
}

void connect_mqtt(void)
{
  Serial.print(F("Checking wifi "));
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(F("."));
    delay(1000);
  }
  Serial.println(F(" connected!"));

  uint8_t retries = 0;
  Serial.print(F("Connecting MQTT "));
  
  while (!client.connect(mqtt_client_name, Configs.username, Configs.mqpassword) and retries < 15)
  {
    Serial.print(".");
    delay(5000);
    retries++;
  }
  if (!client.connected())
    ESP.restart();
  Serial.println(F(" connected!"));

  // we are here only after sucessful MQTT connect
  client.subscribe(light_topic_in);      //subscribe to incoming topic
  sendAutoDiscovery();                   //send auto-discovery topics
  sendStat.attach(2, sendMQTTStatusMsg); //send status of switches
}










void setup() {
Serial.begin(115200);
initRelays();
initButtons();
  for (uint8_t i = 0; i < DEVICES; i++){
lastButtonStates[i] = digitalRead(i);
}

 char NameChipId[64] = {0}, chipId[9] = {0};
 snprintf(chipId, sizeof(chipId), "%06x", ESP.getChipId());
  snprintf(NameChipId, sizeof(NameChipId), "%s_%06x", HOSTNAME, ESP.getChipId());
  WiFi.hostname(const_cast<char *>(NameChipId));

if (!configureWIFI())
if (!wifiConnected) {             // wifi not connected so start the wifi setup web interface
    setupHotspot();
        }

        createGui() ;


 

  client.begin(Configs.server, atoi(mqtt_port), net);
  client.onMessage(messageReceived);

}

void loop() {
readAll();
dnsServer.processNextRequest();
if (shouldUpdateLights) { setRelays();}
}
