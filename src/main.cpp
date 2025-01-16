#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <Adafruit_SSD1306.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "resources.h"


// Preferences & Display Setup
Preferences preferences;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");


// Timers
int last_update = 0;
int update_interval = 2000;

// Display Messages
String display_message = "";

// Machine State
bool machineReady = false;
String localID = "";
String remoteID = "";
String apSSID = "";
String apPass = "";
String secretKey = "";

// HiveMQ Cloud Broker settings
const char *mqtt_server = "0cddc0376612420683d19f92e488db1b.s1.eu.hivemq.cloud";
const char *mqtt_username = "enigma";
const char *mqtt_password = "Enigma123";
const int mqtt_port = 8883;

// Wi-Fi and MQTT Client
WiFiClientSecure secureWifiClient;
PubSubClient mqttClient(secureWifiClient);

// Buzzer Pin
#define BUZZER_PIN 18

// Buzzer Functions
void init_buzzer()
{
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
}

void beep(int duration = 100)
{
    // digitalWrite(BUZZER_PIN, HIGH);
    // delay(duration);
    // digitalWrite(BUZZER_PIN, LOW);
    tone(BUZZER_PIN, 2000, duration);
}

void render_display()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("ID:" + localID);
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    display.setCursor(0, 20);
    display.println(display_message);
    display.drawFastHLine(0, 52, 128, SSD1306_WHITE);
    display.setCursor(0, 55);
    display.println("Paired:" + remoteID);
    display.display();
}

// Display Functions
void set_display_message(String message)
{
    if (display_message != message)
    { // Only beep if the message changes
        beep();
        display_message = message;
        render_display();
    }
}

// --- MQTT Message Callback ---
void msg_callback(char *topic, byte *payload, unsigned int length)
{
    String msg;
    for (int i = 0; i < length; i++)
    {
        msg += (char)payload[i];
    }
    ws.textAll(msg);
    display_message = "MQTT Msg: " + msg;
    render_display();
}

// Wi-Fi and MQTT Functions
void try_wifi()
{
    server.end();
    WiFi.mode(WIFI_STA);
    WiFi.begin(apSSID.c_str(), apPass.c_str());
    set_display_message("Connecting Wi-Fi...");

    if (WiFi.waitForConnectResult() == WL_CONNECTED)
    {
        set_display_message("Wi-Fi Connected!");
        delay(2000);
        server.begin();
    }
    else
    {
        set_display_message("Wi-Fi Failed!");
    }
}

void try_mqtt()
{
    if (mqttClient.connect(localID.c_str(), mqtt_username, mqtt_password))
    {
        mqttClient.setBufferSize(1024);
        mqttClient.setCallback(msg_callback);
        mqttClient.subscribe(localID.c_str());
        set_display_message("MQTT Connected!");
    }
    else
    {
        set_display_message("MQTT Failed!");
    }
}
// --- Generate Local Device ID ---
String getlocalID()
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macStr[13];
    snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

// --- Handle Serial Commands ---
void handle_serial_command(String input)
{
    if (input.startsWith("wifi:"))
    {
        int firstColon = input.indexOf(":");
        int secondColon = input.indexOf(":", firstColon + 1);

        if (secondColon == -1)
        {
            display_message = "Invalid Wi-Fi command!";
            render_display();
            return;
        }

        apSSID = input.substring(firstColon + 1, secondColon);
        apPass = input.substring(secondColon + 1);
        preferences.putString("apSSID", apSSID);
        preferences.putString("apPass", apPass);
        preferences.end();
        set_display_message("Wifi Saved!");
        delay(2000);
        ESP.restart();
    }
    else if (input.startsWith("config:"))
    {
        int col1 = input.indexOf(":");
        int col2 = input.indexOf(":", col1 + 1);
        int col3 = input.indexOf(":", col2 + 1);
        int col4 = input.indexOf(":", col3 + 1);

        if (col1 == -1 || col2 == -1 || col3 == -1 || col4 == -1)
        {
            set_display_message("Invalid config command!");
            return;
        }

        apSSID = input.substring(col1 + 1, col2);
        apPass = input.substring(col2 + 1, col3);
        remoteID = input.substring(col3 + 1, col4);
        secretKey = input.substring(col4 + 1);

        preferences.putString("apSSID", apSSID);
        preferences.putString("apPass", apPass);
        preferences.putString("remoteID", remoteID);
        preferences.putString("secretKey", secretKey);
        preferences.putBool("machineReady", true);
        preferences.end();
        set_display_message("Config Updated!");
        delay(2000);
        ESP.restart();
    }
    else if (input.startsWith("reset-all"))
    {
        preferences.clear();
        preferences.end();
        set_display_message("Preferences Cleared!");
        delay(2000);
        ESP.restart();
    }
    else
    {
        display_message = "Invalid command!";
        render_display();

    }
}

// Serve the HTML page on the root route
void handleRoot(AsyncWebServerRequest *request)
{
    request->send(200, "text/html", root_html_content);
}

// Handle Web Socket Events (Connect, Disconnect, Data)
void wsHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        Serial.println("Websocket client connection received");
    }
    else if (type == WS_EVT_DISCONNECT)
    {
        Serial.println("Websocket client connection closed");
    }
    else if (type == WS_EVT_DATA)
    {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
        {
            data[len] = 0;
            Serial.printf("Websocket client message received: %s\n", (char *)data);
            client->text("Received!");
        }
    }

}

// Setup Function
void setup()
{
    Serial.begin(115200);
    preferences.begin("config", false);
    machineReady = preferences.getBool("machineReady", false);
    if (machineReady)
    {
        localID = getlocalID();
        remoteID = preferences.getString("remoteID", "");
        apSSID = preferences.getString("apSSID", "");
        apPass = preferences.getString("apPass", "");
        secretKey = preferences.getString("secretKey", "");
    }

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    init_buzzer();
    secureWifiClient.setCACert(root_ca);
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(msg_callback);
    server.on("/", HTTP_GET, handleRoot);
    set_display_message(machineReady ? "System Ready!" : "Connect Serial Tool");
}

// Main Loop
void loop()
{
    if (Serial.available() > 0)
    {
        String input = Serial.readString();
        input.trim();
        handle_serial_command(input);
    }

    if (millis() - last_update > update_interval && machineReady)
    {
        if (WiFi.status() != WL_CONNECTED)
            try_wifi();
        if (!mqttClient.connected())
            try_mqtt();

        if (WiFi.status() == WL_CONNECTED && mqttClient.connected())
        {
            mqttClient.loop();
            set_display_message("OK!\nWeb Server Started! \nWeb: " + WiFi.localIP().toString());
        }

        mqttClient.loop();
        last_update = millis();
    }
}
