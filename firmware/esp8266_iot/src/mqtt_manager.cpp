#include "mqtt_manager.h"

MqttManager::MqttManager()
    : _mqttClient(_wifiClient),
      _cmdCb(nullptr),
      _otaCb(nullptr),
      _port(MQTT_PORT_DEFAULT),
      _lastReconnect(0),
      _lastDiscovery(0) {}

void MqttManager::begin(const char* host, uint16_t port,
                         const char* user, const char* pass,
                         const char* deviceId) {
    strncpy(_host, host, sizeof(_host) - 1);
    _port = port;
    strncpy(_user, user, sizeof(_user) - 1);
    strncpy(_pass, pass, sizeof(_pass) - 1);
    strncpy(_deviceId, deviceId, sizeof(_deviceId) - 1);

    // Build topic strings
    snprintf(_topicCmd,    sizeof(_topicCmd),    TOPIC_CMD_TEMPLATE,    _deviceId);
    snprintf(_topicState,  sizeof(_topicState),  TOPIC_STATE_TEMPLATE,  _deviceId);
    snprintf(_topicOta,    sizeof(_topicOta),    TOPIC_OTA_TEMPLATE,    _deviceId);
    snprintf(_topicStatus, sizeof(_topicStatus), TOPIC_STATUS_TEMPLATE, _deviceId);

    // TLS: accept any certificate (HiveMQ Cloud uses trusted CA)
    _wifiClient.setInsecure();

    _mqttClient.setServer(_host, _port);
    _mqttClient.setBufferSize(512);
    _mqttClient.setCallback([this](char* t, byte* p, unsigned int l) {
        handleMessage(t, p, l);
    });

    connect();
}

void MqttManager::loop() {
    if (!_mqttClient.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnect >= MQTT_RECONNECT_MS) {
            _lastReconnect = now;
            connect();
        }
    } else {
        _mqttClient.loop();
    }

    // Periodic discovery broadcast
    if (_mqttClient.connected()) {
        unsigned long now = millis();
        if (now - _lastDiscovery >= DISCOVERY_INTERVAL) {
            _lastDiscovery = now;
            publishDiscovery();
        }
    }
}

bool MqttManager::isConnected() {
    return _mqttClient.connected();
}

void MqttManager::connect() {
    Serial.printf("[MQTT] Connecting to %s:%d as %s...\n", _host, _port, _deviceId);

    // Build LWT payload
    char lwtPayload[64];
    snprintf(lwtPayload, sizeof(lwtPayload), "{\"id\":\"%s\",\"status\":\"offline\"}", _deviceId);

    if (_mqttClient.connect(_deviceId, _user, _pass,
                            _topicStatus, MQTT_QOS,
                            true, lwtPayload)) {
        Serial.println("[MQTT] Connected!");

        // Publish online status (retained)
        char onlinePayload[64];
        snprintf(onlinePayload, sizeof(onlinePayload),
                 "{\"id\":\"%s\",\"status\":\"online\"}", _deviceId);
        _mqttClient.publish(_topicStatus, onlinePayload, true);

        subscribe();
        publishDiscovery();
    } else {
        Serial.printf("[MQTT] Connection failed, rc=%d\n", _mqttClient.state());
    }
}

void MqttManager::subscribe() {
    _mqttClient.subscribe(_topicCmd, MQTT_QOS);
    _mqttClient.subscribe(_topicOta, MQTT_QOS);
    Serial.printf("[MQTT] Subscribed: %s, %s\n", _topicCmd, _topicOta);
}

void MqttManager::handleMessage(char* topic, byte* payload, unsigned int length) {
    char json[512];
    size_t copyLen = min((unsigned int)(sizeof(json) - 1), length);
    memcpy(json, payload, copyLen);
    json[copyLen] = '\0';

    Serial.printf("[MQTT] Received on %s: %s\n", topic, json);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[MQTT] JSON parse error: %s\n", err.c_str());
        return;
    }

    // Handle command messages
    if (strcmp(topic, _topicCmd) == 0 && _cmdCb) {
        const char* feature = doc["feature"];
        bool state = doc["state"] | false;
        if (feature) {
            _cmdCb(feature, state);
        }
    }

    // Handle OTA messages
    if (strcmp(topic, _topicOta) == 0 && _otaCb) {
        const char* cmd = doc["cmd"];
        const char* url = doc["url"];
        if (cmd && strcmp(cmd, "ota") == 0 && url) {
            _otaCb(url);
        }
    }
}

void MqttManager::publishDiscovery() {
    JsonDocument doc;
    doc["id"] = _deviceId;
    doc["type"] = "esp8266";
    doc["v"] = FW_VERSION;

    JsonArray features = doc["features"].to<JsonArray>();
    features.add("relay");
    features.add("led");

    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));

    _mqttClient.publish(TOPIC_DISCOVERY, buffer, false);
    Serial.printf("[MQTT] Discovery sent: %s\n", buffer);
}

void MqttManager::publishState(bool relayState, bool ledState) {
    JsonDocument doc;
    doc["id"] = _deviceId;
    doc["relay"] = relayState;
    doc["led"] = ledState;
    doc["uptime"] = millis() / 1000;
    doc["rssi"] = WiFi.RSSI();

    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));

    _mqttClient.publish(_topicState, buffer, true);
    Serial.printf("[MQTT] State published: %s\n", buffer);
}
