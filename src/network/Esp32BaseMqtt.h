#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class Esp32Base;

class Esp32BaseMqtt {
public:
    enum TransportSecurity : uint8_t {
        TLS,
        EXPLICIT_PLAINTEXT
    };

    enum QualityOfService : uint8_t {
        QOS_0 = 0,
        QOS_1 = 1
    };

    enum State : uint8_t {
        NOT_CONFIGURED,
        CONFIGURATION_ERROR,
        WAITING_FOR_WIFI,
        WAITING_FOR_TIME,
        BACKOFF,
        CONNECTING,
        CONNECTED,
        CONNECTION_REJECTED,
        SUSPENDED_FOR_OTA,
        STOPPING
    };

    enum Error : uint8_t {
        ERROR_NONE,
        ERROR_INVALID_CONFIGURATION,
        ERROR_NOT_CONNECTED,
        ERROR_TOPIC_TOO_LONG,
        ERROR_PAYLOAD_TOO_LONG,
        ERROR_SUBSCRIPTION_LIMIT,
        ERROR_INFLIGHT_LIMIT,
        ERROR_OUTBOX_FULL,
        ERROR_NO_MEMORY,
        ERROR_SUBSCRIBE_FAILED,
        ERROR_DNS_OR_TRANSPORT,
        ERROR_TLS,
        ERROR_TLS_CERTIFICATE,
        ERROR_BROKER_UNAVAILABLE,
        ERROR_PROTOCOL_REJECTED,
        ERROR_CLIENT_ID_REJECTED,
        ERROR_BAD_CREDENTIALS,
        ERROR_NOT_AUTHORIZED,
        ERROR_DISCONNECTED
    };

    enum EventType : uint8_t {
        EVENT_CONNECTED,
        EVENT_DISCONNECTED,
        EVENT_CONNECTION_REJECTED,
        EVENT_SUBSCRIPTION_ACKNOWLEDGED,
        EVENT_SUBSCRIPTION_REJECTED,
        EVENT_PUBLISH_ACKNOWLEDGED,
        EVENT_PUBLISH_DELIVERY_UNCERTAIN,
        EVENT_INCOMING_MESSAGE_DROPPED,
        EVENT_MAILBOX_OVERFLOW
    };

    enum PublishCode : uint8_t {
        PUBLISH_ACCEPTED,
        PUBLISH_NOT_CONFIGURED,
        PUBLISH_NOT_CONNECTED,
        PUBLISH_INVALID_TOPIC,
        PUBLISH_TOPIC_TOO_LONG,
        PUBLISH_INVALID_PAYLOAD,
        PUBLISH_PAYLOAD_TOO_LONG,
        PUBLISH_INVALID_QOS,
        PUBLISH_INFLIGHT_LIMIT,
        PUBLISH_OUTBOX_FULL,
        PUBLISH_NO_MEMORY
    };

    struct TlsCredentials {
        const char* caCertificatePem = nullptr;
        size_t caCertificateLength = 0;
        const char* clientCertificatePem = nullptr;
        size_t clientCertificateLength = 0;
        const char* privateKeyPem = nullptr;
        size_t privateKeyLength = 0;
    };

    struct LastWill {
        const char* topic = nullptr;
        const uint8_t* payload = nullptr;
        size_t payloadLength = 0;
        QualityOfService qos = QOS_0;
        bool retain = false;
    };

    struct ConnectionConfig {
        const char* host = nullptr;
        uint16_t port = 8883;
        const char* clientId = nullptr;
        uint16_t keepAliveSeconds = 60;
        TransportSecurity security = TLS;
        const char* username = nullptr;
        const char* password = nullptr;
        TlsCredentials tls;
        const LastWill* lastWill = nullptr;
    };

    struct Subscription {
        const char* topicFilter = nullptr;
        QualityOfService qos = QOS_0;
    };

    struct PublishRequest {
        const char* topic = nullptr;
        const uint8_t* payload = nullptr;
        size_t payloadLength = 0;
        QualityOfService qos = QOS_0;
        bool retain = false;
    };

    struct PublishResult {
        PublishCode code = PUBLISH_NOT_CONFIGURED;
        uint16_t packetId = 0;

        bool accepted() const { return code == PUBLISH_ACCEPTED; }
    };

    struct MessageView {
        const char* topic = nullptr;
        size_t topicLength = 0;
        const uint8_t* payload = nullptr;
        size_t payloadLength = 0;
        QualityOfService qos = QOS_0;
        bool retain = false;
        bool duplicate = false;
    };

    struct Event {
        EventType type = EVENT_DISCONNECTED;
        Error error = ERROR_NONE;
        uint16_t packetId = 0;
        uint8_t subscriptionIndex = 0xFF;
    };

    struct Status {
        State state = NOT_CONFIGURED;
        Error lastError = ERROR_NONE;
        bool configured = false;
        bool tls = true;
        bool usernameSet = false;
        bool clientCertificateSet = false;
        const char* host = nullptr;
        uint16_t port = 0;
        const char* clientId = nullptr;
        uint32_t connectedAtUptimeMs = 0;
        uint32_t connectedAtEpochSec = 0;
    };

    struct Diagnostics {
        uint32_t connectAttempts = 0;
        uint32_t successfulConnections = 0;
        uint32_t reconnects = 0;
        uint32_t disconnects = 0;
        uint32_t receivedMessages = 0;
        uint32_t receivedBytes = 0;
        uint32_t publishAccepted = 0;
        uint32_t publishAcknowledged = 0;
        uint32_t publishDeliveryUncertain = 0;
        uint32_t subscriptionAcknowledged = 0;
        uint32_t timeCorrectionRetries = 0;
        uint32_t incomingOversizeDropped = 0;
        uint32_t incomingMailboxDropped = 0;
        uint32_t controlEventDropped = 0;
        uint32_t enqueueFailures = 0;
        uint16_t outboxHighWaterBytes = 0;
        uint8_t incomingHighWater = 0;
        uint8_t controlEventHighWater = 0;
        uint8_t inflightQos1 = 0;
        int32_t nativeEspError = 0;
        int32_t nativeTlsError = 0;
        int32_t nativeSocketError = 0;
        uint32_t nativeCertificateFlags = 0;
    };

    typedef void (*MessageCallback)(const MessageView& message, void* context);
    typedef void (*EventCallback)(const Event& event, void* context);

    static bool configure(const ConnectionConfig& config);
    static bool addSubscription(const Subscription& subscription);
    static void setMessageCallback(MessageCallback callback, void* context = nullptr);
    static void setEventCallback(EventCallback callback, void* context = nullptr);
    static PublishResult publish(const PublishRequest& request);
    static bool requestReconnect();

    static State state();
    static Status status();
    static Diagnostics diagnostics();
    static const char* stateName(State state);
    static const char* errorName(Error error);

private:
    friend class Esp32Base;
    static bool begin();
    static void handle(bool otaUploading);
    static void prepareForLifecycleStop();
};
