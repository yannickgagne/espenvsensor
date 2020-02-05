#include <FS.h> //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>
#include <DNSServer.h>
//#include <ESP8266WebServer.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>
#include "DHT.h"

#define mqtt_server "10.10.1.56"
#define mqtt_user "mqttuser"  //s'il a été configuré sur Mosquitto
#define mqtt_password "012mqtT" //idem

#define temperature_topic "sensor01/temperature"  //Topic température
#define humidity_topic "sensor01/humidity"        //Topic humidité

#define BUILTINLED 2 // Pin of blue led on esp board
#define DHTPIN 4    // Pin sur lequel est branché le DHT
#define DHTTYPE DHT22         // DHT 22  (AM2302)

#define SLEEP_PIN 5

//Buffer qui permet de décoder les messages MQTT reçus
char message_buff[100];

long lastMsg = 0;   //Horodatage du dernier message publié sur MQTT
bool debug = true;  //Affiche sur la console si True

//Création des objets
DHT dht(DHTPIN, DHTTYPE);     
WiFiClient espClient;
PubSubClient client(espClient);

// Set web server port number to 80
//WiFiServer server(80);

// Assign output variables to GPIO pins
char mqtt_server_ip[40];
char mqtt_server_port[10];
char mqtt_server_user[40];
char mqtt_server_pass[40];
char mqtt_temp_topic[60];
char mqtt_humi_topic[60];

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//Flash blue led when loop starts
void flash_start() {
  for (size_t i = 0; i < 10; i++)
  {
    digitalWrite(BUILTINLED, !digitalRead(BUILTINLED));
    delay(20);
  }  
}

//Flash blue led when loop ends
void flash_end() {
  for (size_t i = 0; i < 6; i++)
  {
    digitalWrite(BUILTINLED, !digitalRead(BUILTINLED));
    delay(80);
  }  
}

//Reconnexion
void reconnect() {
  //Boucle jusqu'à obtenur une reconnexion
  while (!client.connected()) {
    Serial.print("Connexion au serveur MQTT...");
    if (client.connect("ESP8266Client", mqtt_server_user, mqtt_server_pass)) {
      Serial.println("OK");
    } else {
      Serial.print("KO, erreur : ");
      Serial.print(client.state());
      Serial.println(" On attend 5 secondes avant de recommencer");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // wait for Serial to initialize
  //while (!Serial) { }
  delay(3500);

  // Initialize the output variables as outputs
  pinMode(16, WAKEUP_PULLUP);
  pinMode(BUILTINLED, OUTPUT);
  digitalWrite(BUILTINLED, HIGH);  
  
  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server_ip, json["mqtt_server_ip"]);
          strcpy(mqtt_server_port, json["mqtt_server_port"]);
          strcpy(mqtt_server_user, json["mqtt_server_user"]);
          strcpy(mqtt_server_pass, json["mqtt_server_pass"]);
          strcpy(mqtt_temp_topic, json["mqtt_temp_topic"]);
          strcpy(mqtt_humi_topic, json["mqtt_humi_topic"]);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read config json
  
  WiFiManagerParameter custom_mqtt_server_ip("mqtt_server_ip", "MQTT Server IP", mqtt_server_ip, 40);
  WiFiManagerParameter custom_mqtt_server_port("mqtt_server_port", "MQTT Server Port", mqtt_server_port, 10);
  WiFiManagerParameter custom_mqtt_server_user("mqtt_server_user", "MQTT Server Username", mqtt_server_user, 40);
  WiFiManagerParameter custom_mqtt_server_pass("mqtt_server_pass", "MQTT Server Password", mqtt_server_pass, 40);
  WiFiManagerParameter custom_mqtt_temp_topic("mqtt_temp_topic", "MQTT Temperature Topic", mqtt_temp_topic, 60);
  WiFiManagerParameter custom_mqtt_humi_topic("mqtt_humi_topic", "MQTT Humidity Topic", mqtt_humi_topic, 60);

  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server_ip);
  wifiManager.addParameter(&custom_mqtt_server_port);
  wifiManager.addParameter(&custom_mqtt_server_user);
  wifiManager.addParameter(&custom_mqtt_server_pass);
  wifiManager.addParameter(&custom_mqtt_temp_topic);
  wifiManager.addParameter(&custom_mqtt_humi_topic);
  
  //Uncomment and run it once, if you want to erase all the stored information
  //wifiManager.resetSettings();

  // fetches ssid and pass from eeprom and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP" and goes into a blocking loop awaiting configuration
  //wifiManager.autoConnect("AutoConnectAP");
  // or use this for auto generated name ESP + ChipID
  wifiManager.autoConnect();
  
  // if you get here you have connected to the WiFi
  Serial.println("Connected.");
  
  strcpy(mqtt_server_ip, custom_mqtt_server_ip.getValue());
  strcpy(mqtt_server_port, custom_mqtt_server_port.getValue());
  strcpy(mqtt_server_user, custom_mqtt_server_user.getValue());
  strcpy(mqtt_server_pass, custom_mqtt_server_pass.getValue());
  strcpy(mqtt_temp_topic, custom_mqtt_temp_topic.getValue());
  strcpy(mqtt_humi_topic, custom_mqtt_humi_topic.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server_ip"] = mqtt_server_ip;
    json["mqtt_server_port"] = mqtt_server_port;
    json["mqtt_server_user"] = mqtt_server_user;
    json["mqtt_server_pass"] = mqtt_server_pass;
    json["mqtt_temp_topic"] = mqtt_temp_topic;
    json["mqtt_humi_topic"] = mqtt_humi_topic;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  client.setServer(mqtt_server_ip, atoi(mqtt_server_port));    //Configuration de la connexion au serveur MQTT

  dht.begin();

  // WORK START
  //flash_start();
  if (!client.connected()) {
    reconnect();
  }
  //This should be called regularly to allow the client to process incoming messages and maintain its connection to the server.
  client.loop();

  //Lecture de l'humidité ambiante
  float h = dht.readHumidity();
  // Lecture de la température en Celcius
  float t = dht.readTemperature();

  if ( isnan(t) || isnan(h)) {
    Serial.println("Echec de lecture ! Verifiez votre capteur DHT");
    return;
  }

  if ( debug ) {
    Serial.print("Temperature : ");
    Serial.print(t);
    Serial.print(" | Humidite : ");
    Serial.println(h);
  }  

  client.publish(mqtt_temp_topic, String(t).c_str(), true);   //Publie la température sur le topic temperature_topic
  client.publish(mqtt_humi_topic, String(h).c_str(), true);      //Et l'humidité

  client.disconnect();
  
  //flash_end();
  //goto deep sleep for 5 minutes
  ESP.deepSleep(5000000, WAKE_RF_DEFAULT);
  // WORK END
}

void loop() {
}
