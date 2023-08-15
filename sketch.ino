
#include <WiFi.h>
#include "DHTesp.h"
#include <ArduinoJson.h>
#include "PubSubClient.h"

// WiFi network settings
const char*   ssid = "Wokwi-GUEST";
const char*   password = "";

// MQTT settings
const char*   mqtt_server = "broker.hivemq.com";
const int 		mqtt_port = 1883;
const char*		mqtt_topic_monitor = "Monitoring01";
const char*		mqtt_client_id = "IOT_DEV_01";
const char*		mqtt_topic_control = "Control01";

//RTOS variables
const char*		task_name = "MONITORING_TASK";
constexpr int 	task_memory= 1024*10; 	//Memory task in bytes
constexpr int 	task_core_id= 1; 		//Core ID
TaskHandle_t 	handle_communication_cloud_task=nullptr;
TaskHandle_t 	handle_monitoring_task=nullptr;

//Doc JSON
DynamicJsonDocument doc(1024);

DynamicJsonDocument docReception(1024*5);

//MONITORING
constexpr int	SAMPLING_DATA_MS = 10000;
double 		current_temperature=0;
double 		current_humidity=0;

//PIN DEFINITION
const int DHT_PIN = 25;
const int VALVE_WATER_PIN = 19;
const int COOLING_SYSTEM_PIN = 21;

DHTesp dhtSensor;
char * ptrData=nullptr;
int sizeData=0;

String stMac;
char mac[50];
char clientId[50];

WiFiClient espClient;
PubSubClient client(espClient);



void monitoring_temperature_task(void *pvParameters);
void communication_cloud_task(void *pvParameters);
void fill_data(double _temperature, double _humidity);
void app_set_up_mqtt();
void set_pin_mode();
void setup_sensors();
void app_mqtt_reconnect();

void setup() {
  BaseType_t result;
  Serial.begin(115200);
  
  set_pin_mode();
  setup_sensors();
  app_set_up_wifi();
  app_set_up_mqtt();

  result=xTaskCreate(communication_cloud_task, task_name, task_memory, NULL, task_core_id, &handle_communication_cloud_task);

  if(result!=pdPASS)
  {
    Serial.print("communication_cloud_task error could not be created");
    vTaskDelete(handle_communication_cloud_task);
  }


  result=xTaskCreate(monitoring_temperature_task, task_name, task_memory, NULL, task_core_id, &handle_monitoring_task);

  if(result!=pdPASS)
  {
    Serial.print("monitoring_temperature_task error could not be created");
    vTaskDelete(handle_communication_cloud_task);
  }

}

void setup_sensors()
{
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
}

void set_pin_mode()
{
  pinMode(VALVE_WATER_PIN, OUTPUT);
  pinMode(COOLING_SYSTEM_PIN, OUTPUT);
}


void fill_data(double _temperature, double _humidity)
{
  doc["Temperature"] = _temperature;
  doc["Humidity"] = _humidity;
}

void monitoring_temperature_task(void *pvParameters) {
		
		bool result=false;
    

		while (true)
		{
      Serial.print("Sampling DATA Sensor-> ");
      TempAndHumidity  data = dhtSensor.getTempAndHumidity();
			current_temperature =data.temperature;
			current_humidity =data.humidity;

      Serial.println("Temperature: " + String(data.temperature, 2) + "°C");
      Serial.println("Humidity: " + String(data.humidity, 1) + "%");

      fill_data(current_temperature, current_humidity);

      sizeData=measureJson(doc);
      ptrData=(char*) calloc(1,sizeData+1);

      if(ptrData)
      {
        serializeJson(doc, ptrData,sizeData+1);
      }

      result=client.publish(mqtt_topic_monitor,(uint8_t *)ptrData,sizeData);
			Serial.print("Publish mqtt result: ");
			Serial.print(result==false?"Error can not publish":"Publish successfully");
			Serial.println();

      delay(SAMPLING_DATA_MS);

    }
}



void app_set_up_mqtt()
{
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  while (!client.connect(mqtt_client_id)) 
  {
    delay(500);
    Serial.print(".");
	}

    client.subscribe(mqtt_topic_control);
}

void app_set_up_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.macAddress());
  stMac = WiFi.macAddress();
  stMac.replace(":", "_");
  Serial.println(stMac);
}

void app_mqtt_reconnect() 
{
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    if (client.connect(mqtt_client_id)) {
      Serial.print(mqtt_client_id);
      Serial.println(" connected");
      client.subscribe(mqtt_topic_control);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String stMessage;

  Serial.println();
  /*
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    stMessage += (char)message[i];
  }
  Serial.println();
  */
  if (String(topic) == "Control01") {

    deserializeJson(docReception, message);
    Serial.print("JSON Reception: ");

    String tempControl=  docReception["TemperatureControl"];
   
   Serial.println("tempControl: ");
   if(strcmp(tempControl.c_str(),"ON")==0)
   {
      Serial.println("¡¡¡¡¡¡TURN ON COOLING SYSTEM!!!!!");
      digitalWrite(COOLING_SYSTEM_PIN, HIGH);
   }
   else
   {
      Serial.println("¡¡¡¡¡¡TURN OFF COOLING SYSTEM!!!!!");
      digitalWrite(COOLING_SYSTEM_PIN, LOW);
   }
   
    String humidityControl=  docReception["HumidityControl"];

    Serial.println("humidityControl: ");
    if(strcmp(humidityControl.c_str(),"ON")==0)
    {
      Serial.println("¡¡¡¡¡¡TURN ON VALVE WATER!!!!!");
      digitalWrite(VALVE_WATER_PIN, HIGH);
    }
    else
    {
      Serial.println("¡¡¡¡¡¡TURN OFF VALVE WATER!!!!!");
      digitalWrite(VALVE_WATER_PIN, LOW);
    }
  }
  
}

void communication_cloud_task(void *pvParameters) {
  while (true) {

    if (client.connected()) //Check MQTT Connection
    {
      client.loop();
    }
    else
    {
      app_mqtt_reconnect();
    }
    delay(700);
  }
}

void loop() {
  delay(10);
  /*
  if (!client.connected()) {
    mqttReconnect();
  }
  client.loop();*/
}
