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

// Set web server port number to 80
//WiFiServer server(80);

// Variable to store the HTTP request
String header;

// Auxiliar variables to store the current output state
String outputState = "off";

// Assign output variables to GPIO pins
char output[2] = "5";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//Buffer qui permet de décoder les messages MQTT reçus
char message_buff[100];

long lastMsg = 0;   //Horodatage du dernier message publié sur MQTT
bool debug = true;  //Affiche sur la console si True

#define DHTPIN 4    // Pin sur lequel est branché le DHT
#define DHTTYPE DHT22         // DHT 22  (AM2302)

//Création des objets
DHT dht(DHTPIN, DHTTYPE);     
WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  
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
          strcpy(output, json["output"]);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read config json
  
  WiFiManagerParameter custom_output("output", "output", output, 2);

  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  //add all your parameters here
  wifiManager.addParameter(&custom_output);
  
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
  
  strcpy(output, custom_output.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["output"] = output;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  // Initialize the output variables as outputs
  pinMode(atoi(output), OUTPUT);
  // Set outputs to LOW
  digitalWrite(atoi(output), LOW);;

  client.setServer(mqtt_server, 9001);    //Configuration de la connexion au serveur MQTT

  dht.begin();
}

//Reconnexion
void reconnect() {
  //Boucle jusqu'à obtenur une reconnexion
  while (!client.connected()) {
    Serial.print("Connexion au serveur MQTT...");
    if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {
      Serial.println("OK");
    } else {
      Serial.print("KO, erreur : ");
      Serial.print(client.state());
      Serial.println(" On attend 5 secondes avant de recommencer");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  //This should be called regularly to allow the client to process incoming messages and maintain its connection to the server.
  client.loop();

  long now = millis();
  //Envoi d'un message par minute
  if (now - lastMsg > 1000 * 10) {
    lastMsg = now;
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
    client.publish(temperature_topic, String(t).c_str(), true);   //Publie la température sur le topic temperature_topic
    client.publish(humidity_topic, String(h).c_str(), true);      //Et l'humidité
  }
}
