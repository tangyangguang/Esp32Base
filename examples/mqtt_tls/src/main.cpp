#include <Arduino.h>
#include <Esp32Base.h>

#if __has_include("../local_secrets.h")
#include "../local_secrets.h"
#else
#include "../local_secrets.example.h"
#endif

namespace {

constexpr char COMMAND_TOPIC[] = "example/device/command";
constexpr char STATE_TOPIC[] = "example/device/state";
constexpr uint32_t STATE_PUBLISH_INTERVAL_MS = 60000;
uint32_t g_lastStatePublishMs = 0;

void publishCurrentState() {
    static const uint8_t payload[] = "online";
    Esp32BaseMqtt::PublishRequest request;
    request.topic = STATE_TOPIC;
    request.payload = payload;
    request.payloadLength = sizeof(payload) - 1;
    request.qos = Esp32BaseMqtt::QOS_1;
    request.retain = true;
    const Esp32BaseMqtt::PublishResult result = Esp32BaseMqtt::publish(request);
    if (!result.accepted()) {
        ESP32BASE_LOG_W("app", "mqtt state publish rejected code=%u",
                        static_cast<unsigned>(result.code));
    }
}

void onMqttEvent(const Esp32BaseMqtt::Event& event, void*) {
    if (event.type == Esp32BaseMqtt::EVENT_CONNECTED) {
        // Publish current state after every reconnect. Esp32Base does not retain
        // an application offline queue.
        publishCurrentState();
    } else if (event.type == Esp32BaseMqtt::EVENT_PUBLISH_ACKNOWLEDGED) {
        ESP32BASE_LOG_D("app", "mqtt publish acknowledged packet_id=%u",
                        static_cast<unsigned>(event.packetId));
    } else if (event.type ==
               Esp32BaseMqtt::EVENT_PUBLISH_DELIVERY_UNCERTAIN) {
        // QoS 1 is at-least-once. The Broker may already have the message and
        // ESP-MQTT may retry it, so application commands/state must be idempotent.
        ESP32BASE_LOG_W("app", "mqtt publish delivery uncertain packet_id=%u",
                        static_cast<unsigned>(event.packetId));
    }
}

void onMqttMessage(const Esp32BaseMqtt::MessageView& message, void*) {
    // The pointers are valid only for this callback. Copy data here if the
    // application needs them later. Do not log sensitive command payloads.
    ESP32BASE_LOG_I("app", "mqtt message topic=%.*s payload_bytes=%u qos=%u retain=%s",
                    static_cast<int>(message.topicLength),
                    message.topic,
                    static_cast<unsigned>(message.payloadLength),
                    static_cast<unsigned>(message.qos),
                    message.retain ? "true" : "false");
}

bool configureMqtt() {
    using namespace mqtt_tls_example;

    static const uint8_t offlinePayload[] = "offline";
    static Esp32BaseMqtt::LastWill lastWill;
    lastWill.topic = STATE_TOPIC;
    lastWill.payload = offlinePayload;
    lastWill.payloadLength = sizeof(offlinePayload) - 1;
    lastWill.qos = Esp32BaseMqtt::QOS_1;
    lastWill.retain = true;

    Esp32BaseMqtt::ConnectionConfig connection;
    connection.host = BROKER_HOST;
    connection.port = BROKER_PORT;
    connection.clientId = CLIENT_ID;
    connection.keepAliveSeconds = 60;
    connection.security = Esp32BaseMqtt::TLS;
    connection.username = USERNAME[0] ? USERNAME : nullptr;
    connection.password = PASSWORD[0] ? PASSWORD : nullptr;
    connection.tls.caCertificatePem = CA_CERTIFICATE;
    connection.tls.caCertificateLength = sizeof(CA_CERTIFICATE);
    connection.lastWill = &lastWill;

    if (!Esp32BaseMqtt::configure(connection)) {
        return false;
    }

    Esp32BaseMqtt::Subscription subscription;
    subscription.topicFilter = COMMAND_TOPIC;
    subscription.qos = Esp32BaseMqtt::QOS_1;
    if (!Esp32BaseMqtt::addSubscription(subscription)) {
        return false;
    }
    Esp32BaseMqtt::setMessageCallback(onMqttMessage);
    Esp32BaseMqtt::setEventCallback(onMqttEvent);
    return true;
}

} // namespace

void setup() {
    Esp32Base::setFirmwareInfo("mqtt_tls", "1.0.0");
    if (!configureMqtt()) {
        ESP32BASE_LOG_E("app", "mqtt configuration rejected");
    }
    Esp32Base::begin();
}

void loop() {
    Esp32Base::handle();
    const uint32_t now = millis();
    if (Esp32BaseMqtt::state() == Esp32BaseMqtt::CONNECTED &&
        static_cast<uint32_t>(now - g_lastStatePublishMs) >=
            STATE_PUBLISH_INTERVAL_MS) {
        g_lastStatePublishMs = now;
        publishCurrentState();
    }
    delay(10);
}
