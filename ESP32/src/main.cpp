#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

/* Fill in these two values before uploading. */
#define WIFI_SSID       "REDMI K90"
#define WIFI_PASSWORD   "sbhzj666"

/* OneNET device credentials. Keep the token private. */
#define ONENET_PRODUCT_ID   "KaA9E3nyKZ"
#define ONENET_DEVICE_NAME  "test1"
#define ONENET_MQTT_TOKEN   "version=2018-10-31&res=products%2FKaA9E3nyKZ%2Fdevices%2Ftest1&et=1818174766&method=md5&sign=5upB1tl2yxepvDHqwraUBA%3D%3D"

#define MQTT_HOST       "mqtts.heclouds.com"
#define MQTT_PORT       1883U
#define MQTT_POST_TOPIC "$sys/KaA9E3nyKZ/test1/thing/property/post"
#define MQTT_POST_REPLY_TOPIC "$sys/KaA9E3nyKZ/test1/thing/property/post/reply"
#define MQTT_SET_TOPIC  "$sys/KaA9E3nyKZ/test1/thing/property/set"
#define MQTT_SET_REPLY_TOPIC "$sys/KaA9E3nyKZ/test1/thing/property/set_reply"

#define STM32_UART_RX_PIN 16
#define STM32_UART_TX_PIN 17
#define STM32_BAUDRATE    115200U

#define FRAME_HEAD0       0xAAU
#define FRAME_HEAD1       0x55U
#define FRAME_STATUS      0x01U
#define FRAME_CONTROL     0x81U
#define FRAME_MAX_PAYLOAD 16U

#define NETWORK_RETRY_MS  5000UL
#define MQTT_KEEPALIVE_SECONDS 256U
#define MQTT_SUBSCRIBE_SETTLE_MS 1500UL
#define MQTT_PUBLISH_INTERVAL_MS 5000UL
#define MQTT_BUFFER_SIZE 512U

enum UartRxState
{
    UART_RX_HEAD0 = 0,
    UART_RX_HEAD1,
    UART_RX_TYPE,
    UART_RX_LENGTH,
    UART_RX_PAYLOAD,
    UART_RX_CHECKSUM
};

enum MqttSessionState
{
    MQTT_SESSION_OFFLINE = 0,
    MQTT_SESSION_SUBSCRIBE_SENT,
    MQTT_SESSION_READY
};

struct DeviceStatus
{
    uint8_t temperature;
    uint8_t humidity;
    bool alarmOn;
    bool ledOn;
};

static WiFiClient s_wifiClient;
static PubSubClient s_mqttClient(s_wifiClient);
static UartRxState s_uartRxState = UART_RX_HEAD0;
static uint8_t s_rxType;
static uint8_t s_rxLength;
static uint8_t s_rxIndex;
static uint8_t s_rxChecksum;
static uint8_t s_rxPayload[FRAME_MAX_PAYLOAD];
static DeviceStatus s_status;
static bool s_statusValid = false;
static bool s_remoteAlarmOn = false;
static bool s_remoteLedOn = false;
static bool s_wifiWasConnected = false;
static bool s_mqttWasConnected = false;
static MqttSessionState s_mqttSessionState = MQTT_SESSION_OFFLINE;
static uint32_t s_lastWiFiTryMs = 0UL;
static uint32_t s_lastMqttTryMs = 0UL;
static uint32_t s_subscribeSentMs = 0UL;
static uint32_t s_mqttReadyMs = 0UL;
static uint32_t s_lastPublishMs = 0UL;
static uint32_t s_lastNoStatusLogMs = 0UL;

static bool IsTimeReached(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

static void ResetUartRx(void)
{
    s_uartRxState = UART_RX_HEAD0;
    s_rxLength = 0U;
    s_rxIndex = 0U;
    s_rxChecksum = 0U;
}

static void SendUartFrame(uint8_t type, const uint8_t *payload, uint8_t length)
{
    uint8_t frame[FRAME_MAX_PAYLOAD + 5U];
    uint8_t checksum = type ^ length;

    if (length > FRAME_MAX_PAYLOAD)
    {
        return;
    }

    frame[0] = FRAME_HEAD0;
    frame[1] = FRAME_HEAD1;
    frame[2] = type;
    frame[3] = length;
    for (uint8_t index = 0U; index < length; index++)
    {
        frame[index + 4U] = payload[index];
        checksum ^= payload[index];
    }
    frame[length + 4U] = checksum;
    Serial2.write(frame, length + 5U);
}

static void SendControlToStm32(bool alarmOn, bool ledOn)
{
    const uint8_t payload = (alarmOn ? 0x01U : 0x00U) | (ledOn ? 0x02U : 0x00U);
    SendUartFrame(FRAME_CONTROL, &payload, 1U);
}

static void HandleUartFrame(void)
{
    if ((s_rxType == FRAME_STATUS) && (s_rxLength == 3U))
    {
        s_status.temperature = s_rxPayload[0];
        s_status.humidity = s_rxPayload[1];
        s_status.alarmOn = (s_rxPayload[2] & 0x01U) != 0U;
        s_status.ledOn = (s_rxPayload[2] & 0x02U) != 0U;
        s_statusValid = true;
        Serial.printf("[UART] Temp=%u Hum=%u Alarm=%u Led=%u\r\n",
                      s_status.temperature, s_status.humidity,
                      s_status.alarmOn, s_status.ledOn);
    }
}

static void ProcessUartByte(uint8_t data)
{
    switch (s_uartRxState)
    {
        case UART_RX_HEAD0:
            if (data == FRAME_HEAD0)
            {
                s_uartRxState = UART_RX_HEAD1;
            }
            break;

        case UART_RX_HEAD1:
            if (data == FRAME_HEAD1)
            {
                s_uartRxState = UART_RX_TYPE;
            }
            else if (data != FRAME_HEAD0)
            {
                ResetUartRx();
            }
            break;

        case UART_RX_TYPE:
            s_rxType = data;
            s_rxChecksum = data;
            s_uartRxState = UART_RX_LENGTH;
            break;

        case UART_RX_LENGTH:
            if (data > FRAME_MAX_PAYLOAD)
            {
                ResetUartRx();
                break;
            }
            s_rxLength = data;
            s_rxIndex = 0U;
            s_rxChecksum ^= data;
            s_uartRxState = (data == 0U) ? UART_RX_CHECKSUM : UART_RX_PAYLOAD;
            break;

        case UART_RX_PAYLOAD:
            s_rxPayload[s_rxIndex++] = data;
            s_rxChecksum ^= data;
            if (s_rxIndex >= s_rxLength)
            {
                s_uartRxState = UART_RX_CHECKSUM;
            }
            break;

        case UART_RX_CHECKSUM:
            if (data == s_rxChecksum)
            {
                HandleUartFrame();
            }
            ResetUartRx();
            break;

        default:
            ResetUartRx();
            break;
    }
}

static void ProcessStm32Uart(void)
{
    while (Serial2.available() > 0)
    {
        ProcessUartByte(static_cast<uint8_t>(Serial2.read()));
    }
}

static void PublishPropertySetReply(JsonVariantConst requestId)
{
    StaticJsonDocument<128> document;
    char json[128];
    size_t jsonLength;

    document["id"] = requestId;
    document["code"] = 0;
    document["msg"] = "success";
    jsonLength = serializeJson(document, json, sizeof(json));

    if ((jsonLength > 0U) &&
        s_mqttClient.publish(MQTT_SET_REPLY_TOPIC,
                             reinterpret_cast<const uint8_t *>(json),
                             jsonLength,
                             false))
    {
        Serial.printf("[MQTT] Set reply published: %s\r\n", json);
    }
    else
    {
        Serial.printf("[MQTT] Set reply failed, state=%d\r\n", s_mqttClient.state());
    }
}

static bool ReadControlValue(JsonVariantConst property, bool *value)
{
    JsonVariantConst rawValue = property;

    if ((value == nullptr) || property.isNull())
    {
        return false;
    }
    if (property.is<JsonObjectConst>())
    {
        rawValue = property["value"];
    }
    if (!rawValue.is<bool>())
    {
        return false;
    }

    *value = rawValue.as<bool>();
    return true;
}

static void OnMqttMessage(char *topic, uint8_t *payload, unsigned int length)
{
    StaticJsonDocument<384> document;
    DeserializationError error;
    bool alarmOn;
    bool ledOn;

    Serial.printf("[MQTT] RX topic=%s payload=", topic);
    Serial.write(payload, length);
    Serial.println();

    if (strcmp(topic, MQTT_POST_REPLY_TOPIC) == 0)
    {
        return;
    }

    if (strcmp(topic, MQTT_SET_TOPIC) != 0)
    {
        return;
    }

    error = deserializeJson(document, payload, length);
    if (error)
    {
        Serial.printf("[MQTT] Invalid control JSON: %s\r\n", error.c_str());
        return;
    }

    JsonObject params = document["params"];
    if (params.isNull())
    {
        return;
    }

    alarmOn = s_remoteAlarmOn;
    ledOn = s_remoteLedOn;
    (void)ReadControlValue(params["Alarm"], &alarmOn);
    (void)ReadControlValue(params["Led"], &ledOn);
    s_remoteAlarmOn = alarmOn;
    s_remoteLedOn = ledOn;
    Serial.printf("[MQTT] Control: Alarm=%u Led=%u\r\n",
                  s_remoteAlarmOn, s_remoteLedOn);
    SendControlToStm32(s_remoteAlarmOn, s_remoteLedOn);
    PublishPropertySetReply(document["id"]);
}

static void MaintainWiFi(void)
{
    const uint32_t now = millis();

    if (WiFi.status() == WL_CONNECTED)
    {
        if (!s_wifiWasConnected)
        {
            s_wifiWasConnected = true;
            Serial.printf("[WiFi] Connected, IP=%s\r\n", WiFi.localIP().toString().c_str());
        }
        return;
    }
    if (s_wifiWasConnected)
    {
        s_wifiWasConnected = false;
        s_mqttWasConnected = false;
        s_mqttSessionState = MQTT_SESSION_OFFLINE;
        Serial.println("[WiFi] Disconnected");
    }
    if (IsTimeReached(now, s_lastWiFiTryMs + NETWORK_RETRY_MS))
    {
        s_lastWiFiTryMs = now;
        Serial.printf("[WiFi] Connecting to %s\r\n", WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
}

static void MarkMqttReady(uint32_t now, const char *message)
{
    s_mqttSessionState = MQTT_SESSION_READY;
    s_mqttReadyMs = now;
    Serial.println(message);
}

static void StartMqttSession(uint32_t now)
{
    IPAddress brokerIp;
    bool setSubscribed;
    bool replySubscribed;

    s_lastMqttTryMs = now;
    Serial.println("[MQTT] Connecting to OneNET");
    if (WiFi.hostByName(MQTT_HOST, brokerIp) == 1)
    {
        Serial.printf("[MQTT] DNS=%s\r\n", brokerIp.toString().c_str());
    }
    else
    {
        Serial.println("[MQTT] DNS lookup failed");
    }

    if (!s_mqttClient.connect(ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_MQTT_TOKEN))
    {
        Serial.printf("[MQTT] Connect failed, state=%d\r\n", s_mqttClient.state());
        return;
    }

    setSubscribed = s_mqttClient.subscribe(MQTT_SET_TOPIC);
    replySubscribed = s_mqttClient.subscribe(MQTT_POST_REPLY_TOPIC);
    Serial.printf("[MQTT] Subscribe set=%u post_reply=%u\r\n",
                  setSubscribed, replySubscribed);
    if (setSubscribed && replySubscribed)
    {
        s_mqttSessionState = MQTT_SESSION_SUBSCRIBE_SENT;
        s_subscribeSentMs = now;
    }
    else
    {
        s_mqttSessionState = MQTT_SESSION_OFFLINE;
        Serial.println("[MQTT] Subscribe request failed");
    }
}

static void ServiceMqttConnection(uint32_t now)
{
    const bool receivedData = s_wifiClient.available() > 0;

    if (!s_mqttWasConnected)
    {
        s_mqttWasConnected = true;
        Serial.println("[MQTT] Connected, subscribe sent");
    }
    if (!s_mqttClient.loop())
    {
        Serial.printf("[MQTT] Loop ended, state=%d\r\n", s_mqttClient.state());
        return;
    }
    if ((s_mqttSessionState == MQTT_SESSION_SUBSCRIBE_SENT) && receivedData)
    {
        MarkMqttReady(now, "[MQTT] Subscribe response received");
    }
    else if ((s_mqttSessionState == MQTT_SESSION_SUBSCRIBE_SENT) &&
             IsTimeReached(now, s_subscribeSentMs + MQTT_SUBSCRIBE_SETTLE_MS))
    {
        MarkMqttReady(now, "[MQTT] Subscribe settle complete");
    }
}

static void MaintainMqtt(void)
{
    const uint32_t now = millis();

    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }
    if (s_mqttClient.connected())
    {
        ServiceMqttConnection(now);
        return;
    }
    if (s_mqttWasConnected)
    {
        s_mqttWasConnected = false;
        s_mqttSessionState = MQTT_SESSION_OFFLINE;
        Serial.printf("[MQTT] Disconnected, state=%d\r\n", s_mqttClient.state());
    }
    if (IsTimeReached(now, s_lastMqttTryMs + NETWORK_RETRY_MS))
    {
        StartMqttSession(now);
    }
}

static void PublishStatus(void)
{
    StaticJsonDocument<384> document;
    char json[256];
    size_t jsonLength;
    const uint32_t now = millis();

    if ((!s_mqttClient.connected()) || (s_mqttSessionState != MQTT_SESSION_READY))
    {
        return;
    }
    if (!s_statusValid)
    {
        if (IsTimeReached(now, s_lastNoStatusLogMs + MQTT_PUBLISH_INTERVAL_MS))
        {
            s_lastNoStatusLogMs = now;
            Serial.println("[MQTT] Waiting for STM32 status before publish");
        }
        return;
    }
    if (!IsTimeReached(now, s_mqttReadyMs + MQTT_PUBLISH_INTERVAL_MS) ||
        !IsTimeReached(now, s_lastPublishMs + MQTT_PUBLISH_INTERVAL_MS))
    {
        return;
    }

    document["id"] = "123";
    document["params"]["Temp"]["value"] = s_status.temperature;
    document["params"]["Hum"]["value"] = s_status.humidity;
    document["params"]["Alarm"]["value"] = s_status.alarmOn;
    document["params"]["Led"]["value"] = s_status.ledOn;
    jsonLength = serializeJson(document, json, sizeof(json));
    Serial.printf("[MQTT] TX topic=%s payload=%s\r\n", MQTT_POST_TOPIC, json);
    if ((jsonLength > 0U) &&
        s_mqttClient.publish(MQTT_POST_TOPIC,
                             reinterpret_cast<const uint8_t *>(json),
                             jsonLength,
                             false))
    {
        s_lastPublishMs = now;
        Serial.println("[MQTT] Status published");
    }
    else
    {
        Serial.printf("[MQTT] Publish failed, state=%d\r\n", s_mqttClient.state());
    }
}

void setup()
{
    Serial.begin(115200U);
    Serial2.begin(STM32_BAUDRATE, SERIAL_8N1, STM32_UART_RX_PIN, STM32_UART_TX_PIN);
    ResetUartRx();

    WiFi.mode(WIFI_STA);
    s_mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    s_mqttClient.setCallback(OnMqttMessage);
    s_mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
    s_mqttClient.setKeepAlive(MQTT_KEEPALIVE_SECONDS);
}

void loop()
{
    ProcessStm32Uart();
    MaintainWiFi();
    MaintainMqtt();
    PublishStatus();
}
