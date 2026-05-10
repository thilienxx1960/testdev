#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

class MqttManager {
public:
    using CommandCallback = std::function<void(const char* feature, bool state)>;
    using OtaCallback = std::function<void(const char* url)>;

    MqttManager();

    void begin(const char* host, uint16_t port,
               const char* user, const char* pass,
               const char* deviceId);
    void loop();
    bool isConnected();

    void publishDiscovery();
    void publishState(bool relayState, bool ledState);

    void onCommand(CommandCallback cb)  { _cmdCb = cb; }
    void onOta(OtaCallback cb)          { _otaCb = cb; }

private:
    void connect();
    void subscribe();
    void handleMessage(char* topic, byte* payload, unsigned int length);

    WiFiClientSecure _wifiClient;
    PubSubClient     _mqttClient;
    CommandCallback  _cmdCb;
    OtaCallback      _otaCb;

    char _host[128];
    uint16_t _port;
    char _user[64];
    char _pass[64];
    char _deviceId[32];

    char _topicCmd[64];
    char _topicState[64];
    char _topicOta[64];
    char _topicStatus[64];

    unsigned long _lastReconnect;
    unsigned long _lastDiscovery;
};

#endif // MQTT_MANAGER_H
