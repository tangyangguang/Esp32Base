#include <unity.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../../src/Esp32BaseProfile.h"
#define private public
#include "../../src/network/Esp32BaseMqtt.h"
#undef private

#define ESP32BASE_LOG_I(...) ((void)0)
#define ESP32BASE_LOG_W(...) ((void)0)
#define ESP32BASE_LOG_D(...) ((void)0)
#define ESP32BASE_LOG_E(...) ((void)0)

bool g_fakeWifiConnected = false;
bool g_fakeRealTime = false;
uint32_t g_fakeMillis = 0;
uint32_t g_fakeRandom = 1;

class Esp32BaseWiFi {
public:
    static bool isConnected() { return g_fakeWifiConnected; }
};

class Esp32BaseTime {
public:
    enum Source : uint8_t {
        SOURCE_UPTIME = 0,
        SOURCE_RTC = 1,
        SOURCE_NTP = 2
    };
    struct Snapshot {
        bool synced;
        Source source;
        uint32_t epochSec;
    };
    static Source g_source;
    static bool isRealTime() { return g_fakeRealTime; }
    static Snapshot snapshot() {
        return {
            g_fakeRealTime,
            g_fakeRealTime ? g_source : SOURCE_UPTIME,
            g_fakeRealTime ? 1800000000u : 0u
        };
    }
};

Esp32BaseTime::Source Esp32BaseTime::g_source = Esp32BaseTime::SOURCE_NTP;

#include "mqtt_client.h"

struct esp_mqtt_client {
    int unused;
};

static esp_mqtt_client g_fakeClient;
static esp_event_handler_t g_fakeEventHandler = nullptr;
static esp_mqtt_client_config_t g_lastNativeConfig = {};
static int g_fakePacketId = 10;
static int g_fakeOutboxBytes = 0;
static int g_fakeSubscribeCount = 0;
static int g_fakeEnqueueResult = 0;
static int g_fakeStopCount = 0;
static int g_fakeDisconnectCount = 0;
static esp_err_t g_fakeDisconnectResult = ESP_OK;
static int g_fakeSetConfigCount = 0;
static esp_err_t g_fakeSetConfigResult = ESP_OK;
static char g_lastWillPayload[96] = {};

void captureNativeConfig(const esp_mqtt_client_config_t* config) {
    g_lastNativeConfig = *config;
    g_lastWillPayload[0] = '\0';
    if (config->lwt_msg && config->lwt_msg_len > 0) {
        const size_t length = static_cast<size_t>(config->lwt_msg_len) <
                                      sizeof(g_lastWillPayload) - 1
                                  ? static_cast<size_t>(config->lwt_msg_len)
                                  : sizeof(g_lastWillPayload) - 1;
        memcpy(g_lastWillPayload, config->lwt_msg, length);
        g_lastWillPayload[length] = '\0';
    }
}

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* config) {
    captureNativeConfig(config);
    return &g_fakeClient;
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t) { return ESP_OK; }
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t) {
    ++g_fakeStopCount;
    return ESP_OK;
}
esp_err_t esp_mqtt_client_disconnect(esp_mqtt_client_handle_t) {
    ++g_fakeDisconnectCount;
    return g_fakeDisconnectResult;
}
esp_err_t esp_mqtt_client_reconnect(esp_mqtt_client_handle_t) { return ESP_OK; }
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t) { return ESP_OK; }
esp_err_t esp_mqtt_set_config(
    esp_mqtt_client_handle_t,
    const esp_mqtt_client_config_t* config) {
    ++g_fakeSetConfigCount;
    if (g_fakeSetConfigResult == ESP_OK) {
        captureNativeConfig(config);
    }
    return g_fakeSetConfigResult;
}

esp_err_t esp_mqtt_client_register_event(
    esp_mqtt_client_handle_t,
    esp_mqtt_event_id_t,
    esp_event_handler_t handler,
    void*) {
    g_fakeEventHandler = handler;
    return ESP_OK;
}

int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char*, int) {
    ++g_fakeSubscribeCount;
    return ++g_fakePacketId;
}

int esp_mqtt_client_enqueue(
    esp_mqtt_client_handle_t,
    const char*,
    const char*,
    int length,
    int,
    int,
    bool) {
    if (g_fakeEnqueueResult < 0) {
        return g_fakeEnqueueResult;
    }
    g_fakeOutboxBytes += length + 32;
    return ++g_fakePacketId;
}

int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t) {
    return g_fakeOutboxBytes;
}

#include "../../src/network/Esp32BaseMqtt.inc"

namespace {

static const char TEST_CA[] =
    "-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n";

int g_applicationEventCount = 0;
Esp32BaseMqtt::Event g_lastApplicationEvent;
bool g_tryPublishOnUncertain = false;
Esp32BaseMqtt::PublishCode g_reentrantPublishCode =
    Esp32BaseMqtt::PUBLISH_ACCEPTED;
int g_messageCount = 0;
size_t g_lastPayloadLength = 0;
char g_lastTopic[ESP32BASE_MQTT_MAX_TOPIC_BYTES + 1] = {};
int g_beforeConnectCount = 0;
char g_dynamicWillPayload[96] = {};
Esp32BaseMqtt::LastWill* g_dynamicWill = nullptr;
bool g_makeDynamicWillInvalid = false;

void beforeConnect(void*) {
    ++g_beforeConnectCount;
    if (!g_dynamicWill) {
        return;
    }
    snprintf(g_dynamicWillPayload,
             sizeof(g_dynamicWillPayload),
             "offline-%d",
             g_beforeConnectCount);
    g_dynamicWill->payload =
        reinterpret_cast<const uint8_t*>(g_dynamicWillPayload);
    g_dynamicWill->payloadLength = g_makeDynamicWillInvalid
                                       ? ESP32BASE_MQTT_MAX_PAYLOAD_BYTES + 1
                                       : strlen(g_dynamicWillPayload);
}

void onEvent(const Esp32BaseMqtt::Event& event, void*) {
    ++g_applicationEventCount;
    g_lastApplicationEvent = event;
    if (g_tryPublishOnUncertain &&
        event.type == Esp32BaseMqtt::EVENT_PUBLISH_DELIVERY_UNCERTAIN) {
        static const uint8_t payload[] = "new";
        Esp32BaseMqtt::PublishRequest request;
        request.topic = "device/state";
        request.payload = payload;
        request.payloadLength = sizeof(payload) - 1;
        request.qos = Esp32BaseMqtt::QOS_1;
        g_reentrantPublishCode = Esp32BaseMqtt::publish(request).code;
    }
}

void onMessage(const Esp32BaseMqtt::MessageView& message, void*) {
    ++g_messageCount;
    g_lastPayloadLength = message.payloadLength;
    memcpy(g_lastTopic, message.topic, message.topicLength);
    g_lastTopic[message.topicLength] = '\0';
}

void resetModule() {
    if (g_mqttClient) {
        g_mqttClient = nullptr;
    }
    g_mqttConfig = {};
    memset(g_mqttSubscriptions, 0, sizeof(g_mqttSubscriptions));
    memset(g_mqttInflight, 0, sizeof(g_mqttInflight));
    memset(g_mqttIncoming, 0, sizeof(g_mqttIncoming));
    memset(g_mqttEvents, 0, sizeof(g_mqttEvents));
    g_mqttSubscriptionCount = 0;
    g_mqttEventRead = 0;
    g_mqttEventWrite = 0;
    g_mqttEventCount = 0;
    g_mqttAssemblySlot = -1;
    g_mqttDroppingPacketId = -1;
    g_mqttConfigured = false;
    g_mqttConfigurationAttempted = false;
    g_mqttBegun = false;
    g_mqttTerminalRejection = false;
    g_mqttConnectionConfigurationFailed = false;
    g_mqttEverConnected = false;
    g_mqttOtaSuspended = false;
    g_mqttReconnectRequested = false;
    g_mqttForceReconnectAfterDisconnect = false;
    g_mqttConnectionWanted = true;
    g_mqttNativeConnected = false;
    g_mqttShutdownActive = g_mqttShutdownAcked = g_mqttMaintenanceDraining = false;
    g_fakeDisconnectResult = ESP_OK;
    g_mqttShutdownPacket = 0;
    g_mqttShutdownDeadline = g_mqttShutdownMailboxDrops = 0;
    g_mqttShutdownResult = Esp32BaseMqtt::SHUTDOWN_NONE;
    g_mqttClient = nullptr;
    g_mqttState = Esp32BaseMqtt::NOT_CONFIGURED;
    g_mqttLastError = Esp32BaseMqtt::ERROR_NONE;
    g_mqttPendingDisconnectError = Esp32BaseMqtt::ERROR_DISCONNECTED;
    g_mqttDiagnostics = {};
    g_mqttNextAttemptMs = 0;
    g_mqttBackoffStep = 0;
    g_mqttReportedMailboxDrops = 0;
    g_mqttReportedIncomingDrops = 0;
    g_mqttConnectedAtUptimeMs = 0;
    g_mqttConnectedAtEpochSec = 0;
    g_mqttMessageCallback = nullptr;
    g_mqttMessageContext = nullptr;
    g_mqttEventCallback = nullptr;
    g_mqttEventContext = nullptr;
    g_mqttBeforeConnectCallback = nullptr;
    g_mqttBeforeConnectContext = nullptr;
    g_fakeWifiConnected = false;
    g_fakeRealTime = false;
    Esp32BaseTime::g_source = Esp32BaseTime::SOURCE_NTP;
    g_fakeMillis = 0;
    g_fakeRandom = 1;
    g_fakeEventHandler = nullptr;
    g_lastNativeConfig = {};
    g_fakePacketId = 10;
    g_fakeOutboxBytes = 0;
    g_fakeSubscribeCount = 0;
    g_fakeEnqueueResult = 0;
    g_fakeStopCount = 0;
    g_fakeDisconnectCount = 0;
    g_fakeSetConfigCount = 0;
    g_fakeSetConfigResult = ESP_OK;
    g_lastWillPayload[0] = '\0';
    g_applicationEventCount = 0;
    g_tryPublishOnUncertain = false;
    g_reentrantPublishCode = Esp32BaseMqtt::PUBLISH_ACCEPTED;
    g_messageCount = 0;
    g_lastPayloadLength = 0;
    g_lastTopic[0] = '\0';
    g_beforeConnectCount = 0;
    g_dynamicWillPayload[0] = '\0';
    g_dynamicWill = nullptr;
    g_makeDynamicWillInvalid = false;
}

Esp32BaseMqtt::ConnectionConfig validConfig() {
    Esp32BaseMqtt::ConnectionConfig config;
    config.host = "broker.example";
    config.port = 8883;
    config.clientId = "native-test";
    config.security = Esp32BaseMqtt::TLS;
    config.tls.caCertificatePem = TEST_CA;
    config.tls.caCertificateLength = sizeof(TEST_CA);
    return config;
}

void emitEvent(esp_mqtt_event_id_t eventId,
               int packetId = 0,
               esp_mqtt_error_codes_t* error = nullptr) {
    esp_mqtt_event_t event = {};
    event.event_id = eventId;
    event.client = &g_fakeClient;
    event.msg_id = packetId;
    event.error_handle = error;
    g_fakeEventHandler(nullptr, nullptr, eventId, &event);
}

void startAndConnect() {
    TEST_ASSERT_TRUE(Esp32BaseMqtt::configure(validConfig()));
    TEST_ASSERT_TRUE(Esp32BaseMqtt::begin());
    g_fakeWifiConnected = true;
    g_fakeRealTime = true;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONNECTING, Esp32BaseMqtt::state());
    emitEvent(MQTT_EVENT_BEFORE_CONNECT);
    emitEvent(MQTT_EVENT_CONNECTED);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONNECTED, Esp32BaseMqtt::state());
}

void test_configuration_requires_ca_and_rejects_plaintext_by_default() {
    Esp32BaseMqtt::ConnectionConfig config = validConfig();
    config.tls.caCertificatePem = nullptr;
    config.tls.caCertificateLength = 0;
    TEST_ASSERT_FALSE(Esp32BaseMqtt::configure(config));
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONFIGURATION_ERROR,
                      Esp32BaseMqtt::state());

    resetModule();
    config = validConfig();
    config.tls.caCertificateLength =
        ESP32BASE_MQTT_MAX_CA_CERT_BYTES + 1;
    TEST_ASSERT_FALSE(Esp32BaseMqtt::configure(config));

    resetModule();
    static const char disableVerification[] = "NULL";
    config = validConfig();
    config.tls.caCertificatePem = disableVerification;
    config.tls.caCertificateLength = sizeof(disableVerification);
    TEST_ASSERT_FALSE(Esp32BaseMqtt::configure(config));

    resetModule();
    config = validConfig();
    config.security = Esp32BaseMqtt::EXPLICIT_PLAINTEXT;
    config.tls = {};
    TEST_ASSERT_FALSE(Esp32BaseMqtt::configure(config));
}

void test_begin_never_starts_network_and_waits_for_prerequisites() {
    TEST_ASSERT_TRUE(Esp32BaseMqtt::configure(validConfig()));
    TEST_ASSERT_TRUE(Esp32BaseMqtt::begin());
    TEST_ASSERT_NULL(g_fakeEventHandler);

    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::WAITING_FOR_WIFI,
                      Esp32BaseMqtt::state());

    g_fakeWifiConnected = true;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::WAITING_FOR_TIME,
                      Esp32BaseMqtt::state());

    g_fakeRealTime = true;
    Esp32BaseTime::g_source = Esp32BaseTime::SOURCE_RTC;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::WAITING_FOR_TIME,
                      Esp32BaseMqtt::state());

    Esp32BaseTime::g_source = Esp32BaseTime::SOURCE_NTP;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONNECTING,
                      Esp32BaseMqtt::state());
}

void test_callbacks_are_deferred_until_handle_and_subscriptions_repeat() {
    Esp32BaseMqtt::Subscription subscription;
    subscription.topicFilter = "device/+/command";
    subscription.qos = Esp32BaseMqtt::QOS_1;
    TEST_ASSERT_TRUE(Esp32BaseMqtt::addSubscription(subscription));
    Esp32BaseMqtt::setEventCallback(onEvent);
    startAndConnect();
    TEST_ASSERT_EQUAL(1, g_fakeSubscribeCount);
    TEST_ASSERT_EQUAL(1, g_applicationEventCount);

    char rejectedQos = static_cast<char>(0x80);
    esp_mqtt_event_t rejected = {};
    rejected.client = &g_fakeClient;
    rejected.msg_id = g_mqttSubscriptions[0].pendingPacketId;
    rejected.data = &rejectedQos;
    rejected.data_len = 1;
    g_fakeEventHandler(nullptr, nullptr, MQTT_EVENT_SUBSCRIBED, &rejected);
    TEST_ASSERT_EQUAL(1, g_applicationEventCount);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(2, g_applicationEventCount);
    TEST_ASSERT_EQUAL(0,
                      Esp32BaseMqtt::diagnostics().subscriptionAcknowledged);

    emitEvent(MQTT_EVENT_DISCONNECTED);
    TEST_ASSERT_EQUAL(2, g_applicationEventCount);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::BACKOFF, Esp32BaseMqtt::state());

    g_fakeMillis = g_mqttNextAttemptMs;
    Esp32BaseMqtt::handle(false);
    emitEvent(MQTT_EVENT_BEFORE_CONNECT);
    emitEvent(MQTT_EVENT_CONNECTED);
    TEST_ASSERT_EQUAL(3, g_applicationEventCount);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(2, g_fakeSubscribeCount);
    TEST_ASSERT_TRUE(g_applicationEventCount >= 4);
}

void test_subscription_grant_survives_deferred_dispatch() {
    Esp32BaseMqtt::Subscription subscription;
    subscription.topicFilter = "device/command";
    subscription.qos = Esp32BaseMqtt::QOS_1;
    TEST_ASSERT_TRUE(Esp32BaseMqtt::addSubscription(subscription));
    Esp32BaseMqtt::setEventCallback(onEvent);
    startAndConnect();
    TEST_ASSERT_EQUAL_UINT8(0xFF, g_lastApplicationEvent.grantedQos);
    const struct { int length; uint8_t code; bool missing; } cases[] = {
        {1, 0, false}, {1, 1, false}, {1, 2, false},
        {1, 0x80, false}, {0, 1, false}, {1, 1, true},
        {2, 1, false}, {1, 3, false}, {-1, 1, false}
    };
    for (const auto& item : cases) {
        char codes[] = {static_cast<char>(item.code), 1};
        esp_mqtt_event_t event = {};
        event.client = &g_fakeClient;
        event.msg_id = 123;
        event.data = item.missing ? nullptr : codes;
        event.data_len = item.length;
        g_mqttSubscriptions[0].pendingPacketId = 123;
        const int before = g_applicationEventCount;
        g_fakeEventHandler(nullptr, nullptr, MQTT_EVENT_SUBSCRIBED, &event);
        TEST_ASSERT_EQUAL(before, g_applicationEventCount);
        codes[0] = 42; // Native callback data must not be borrowed after return.
        Esp32BaseMqtt::handle(false);
        TEST_ASSERT_EQUAL(before + 1, g_applicationEventCount);
        TEST_ASSERT_EQUAL_UINT8(0, g_lastApplicationEvent.subscriptionIndex);
        TEST_ASSERT_EQUAL_UINT16(123, g_lastApplicationEvent.packetId);
        TEST_ASSERT_EQUAL_UINT8(
            item.length == 1 && !item.missing && item.code <= 2
                ? item.code : 0xFF, g_lastApplicationEvent.grantedQos);
        TEST_ASSERT_EQUAL(item.code == 0x80
            ? Esp32BaseMqtt::EVENT_SUBSCRIPTION_REJECTED
            : Esp32BaseMqtt::EVENT_SUBSCRIPTION_ACKNOWLEDGED,
            g_lastApplicationEvent.type);
    }
}

void test_qos1_publish_ack_and_outbox_limit() {
    Esp32BaseMqtt::setEventCallback(onEvent);
    startAndConnect();
    const uint8_t payload[] = {1, 2, 3};
    Esp32BaseMqtt::PublishRequest request;
    request.topic = "device/state";
    request.payload = payload;
    request.payloadLength = sizeof(payload);
    request.qos = Esp32BaseMqtt::QOS_1;
    const Esp32BaseMqtt::PublishResult result = Esp32BaseMqtt::publish(request);
    TEST_ASSERT_TRUE(result.accepted());
    TEST_ASSERT_EQUAL(1, Esp32BaseMqtt::diagnostics().inflightQos1);

    emitEvent(MQTT_EVENT_PUBLISHED, result.packetId);
    TEST_ASSERT_EQUAL(1, Esp32BaseMqtt::diagnostics().inflightQos1);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(0, Esp32BaseMqtt::diagnostics().inflightQos1);
    TEST_ASSERT_EQUAL(1, Esp32BaseMqtt::diagnostics().publishAcknowledged);

    g_fakeOutboxBytes = ESP32BASE_MQTT_MAX_OUTBOX_BYTES;
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::PUBLISH_OUTBOX_FULL,
                      Esp32BaseMqtt::publish(request).code);
    g_fakeOutboxBytes = 0;

    const Esp32BaseMqtt::PublishResult uncertain =
        Esp32BaseMqtt::publish(request);
    TEST_ASSERT_TRUE(uncertain.accepted());
    g_tryPublishOnUncertain = true;
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(0, Esp32BaseMqtt::diagnostics().inflightQos1);
    TEST_ASSERT_EQUAL(
        1, Esp32BaseMqtt::diagnostics().publishDeliveryUncertain);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::PUBLISH_NOT_CONNECTED,
                      g_reentrantPublishCode);
}

void test_disconnect_discards_queued_and_partial_incoming_messages() {
    Esp32BaseMqtt::setMessageCallback(onMessage);
    startAndConnect();
    char topic[] = "device/command";
    char data[] = "abc";
    esp_mqtt_event_t incoming = {};
    incoming.client = &g_fakeClient;
    incoming.event_id = MQTT_EVENT_DATA;
    incoming.topic = topic;
    incoming.topic_len = strlen(topic);
    incoming.data = data;
    incoming.data_len = 3;
    incoming.total_data_len = 3;
    incoming.msg_id = 1;
    handleIncomingData(&incoming);
    incoming.msg_id = 2;
    incoming.total_data_len = 6;
    handleIncomingData(&incoming);
    TEST_ASSERT_EQUAL(2, incomingUsedLocked());
    emitEvent(MQTT_EVENT_DISCONNECTED);
    TEST_ASSERT_EQUAL(0, incomingUsedLocked());
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(0, g_messageCount);
    TEST_ASSERT_EQUAL_UINT32(2, Esp32BaseMqtt::diagnostics().incomingMailboxDropped);
}

void test_ota_preparation_does_not_dispatch_application_callbacks() {
    startAndConnect();
    const int before = g_applicationEventCount;
    emitEvent(MQTT_EVENT_PUBLISHED, 42);
    esp32base_internal::suspendMqttForOta();
    TEST_ASSERT_EQUAL(before, g_applicationEventCount);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SUSPENDED_FOR_OTA, Esp32BaseMqtt::state());
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(true);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SUSPENDED_FOR_OTA, Esp32BaseMqtt::state());
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_NOT_EQUAL(Esp32BaseMqtt::SUSPENDED_FOR_OTA, Esp32BaseMqtt::state());
}

void test_publish_capacity_does_not_expand_incoming_slots() {
    TEST_ASSERT_EQUAL_UINT32(ESP32BASE_MQTT_MAX_INCOMING_PAYLOAD_BYTES,
                             sizeof(g_mqttIncoming[0].payload));
    startAndConnect();
    static uint8_t payload[ESP32BASE_MQTT_MAX_PAYLOAD_BYTES] = {};
    Esp32BaseMqtt::PublishRequest request;
    request.topic = "device/state";
    request.payload = payload;
    request.payloadLength = sizeof(payload);
    request.qos = Esp32BaseMqtt::QOS_1;
    TEST_ASSERT_TRUE(Esp32BaseMqtt::publish(request).accepted());
}

void test_fragment_assembly_and_oversize_drop() {
    Esp32BaseMqtt::setMessageCallback(onMessage);
    startAndConnect();
    char topic[] = "device/command";
    char first[] = "abc";
    char second[] = "def";
    esp_mqtt_event_t event = {};
    event.client = &g_fakeClient;
    event.event_id = MQTT_EVENT_DATA;
    event.msg_id = 55;
    event.topic = topic;
    event.topic_len = strlen(topic);
    event.data = first;
    event.data_len = 3;
    event.total_data_len = 6;
    event.current_data_offset = 0;
    g_fakeEventHandler(nullptr, nullptr, MQTT_EVENT_DATA, &event);
    TEST_ASSERT_EQUAL(0, g_messageCount);

    event.topic = nullptr;
    event.topic_len = 0;
    event.data = second;
    event.current_data_offset = 3;
    g_fakeEventHandler(nullptr, nullptr, MQTT_EVENT_DATA, &event);
    TEST_ASSERT_EQUAL(0, g_messageCount);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(1, g_messageCount);
    TEST_ASSERT_EQUAL(6, g_lastPayloadLength);
    TEST_ASSERT_EQUAL_STRING("device/command", g_lastTopic);

    event.msg_id = 56;
    event.topic = topic;
    event.topic_len = strlen(topic);
    event.total_data_len = ESP32BASE_MQTT_MAX_INCOMING_PAYLOAD_BYTES + 1;
    event.current_data_offset = 0;
    g_fakeEventHandler(nullptr, nullptr, MQTT_EVENT_DATA, &event);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(1, g_messageCount);
    TEST_ASSERT_EQUAL(1, Esp32BaseMqtt::diagnostics().incomingOversizeDropped);

    event.msg_id = 57;
    event.total_data_len = INT_MAX;
    event.current_data_offset = INT_MAX;
    event.data_len = INT_MAX;
    g_fakeEventHandler(nullptr, nullptr, MQTT_EVENT_DATA, &event);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(1, g_messageCount);
    TEST_ASSERT_EQUAL(2, Esp32BaseMqtt::diagnostics().incomingOversizeDropped);
}

void test_terminal_auth_rejection_requires_explicit_retry() {
    startAndConnect();
    esp_mqtt_error_codes_t error = {};
    error.error_type = MQTT_ERROR_TYPE_CONNECTION_REFUSED;
    error.connect_return_code = MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED;
    emitEvent(MQTT_EVENT_ERROR, 0, &error);
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONNECTION_REJECTED,
                      Esp32BaseMqtt::state());
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::ERROR_NOT_AUTHORIZED,
                      Esp32BaseMqtt::status().lastError);
    TEST_ASSERT_TRUE(Esp32BaseMqtt::requestReconnect());
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::BACKOFF, Esp32BaseMqtt::state());
}

void test_lwt_is_mapped_and_ota_suspends_without_direct_callback() {
    static const uint8_t willPayload[] = "offline";
    Esp32BaseMqtt::LastWill will;
    will.topic = "device/availability";
    will.payload = willPayload;
    will.payloadLength = sizeof(willPayload) - 1;
    will.qos = Esp32BaseMqtt::QOS_1;
    will.retain = true;
    Esp32BaseMqtt::ConnectionConfig config = validConfig();
    config.lastWill = &will;
    TEST_ASSERT_TRUE(Esp32BaseMqtt::configure(config));
    Esp32BaseMqtt::setEventCallback(onEvent);
    TEST_ASSERT_TRUE(Esp32BaseMqtt::begin());
    g_fakeWifiConnected = true;
    g_fakeRealTime = true;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL_STRING("device/availability", g_lastNativeConfig.lwt_topic);
    TEST_ASSERT_EQUAL(7, g_lastNativeConfig.lwt_msg_len);
    TEST_ASSERT_EQUAL(1, g_lastNativeConfig.lwt_qos);
    TEST_ASSERT_EQUAL(1, g_lastNativeConfig.lwt_retain);

    emitEvent(MQTT_EVENT_CONNECTED);
    TEST_ASSERT_EQUAL(0, g_applicationEventCount);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(1, g_applicationEventCount);
    Esp32BaseMqtt::handle(true);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SUSPENDED_FOR_OTA,
                      Esp32BaseMqtt::state());
    TEST_ASSERT_EQUAL(1, g_applicationEventCount);
    TEST_ASSERT_EQUAL(1, g_fakeDisconnectCount);
    TEST_ASSERT_EQUAL(0, g_fakeStopCount);
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(true);
    TEST_ASSERT_EQUAL(2, g_applicationEventCount);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SUSPENDED_FOR_OTA,
                      Esp32BaseMqtt::state());
}

void test_before_connect_refreshes_lwt_for_every_attempt() {
    Esp32BaseMqtt::LastWill will;
    will.topic = "device/availability";
    will.payload = reinterpret_cast<const uint8_t*>(g_dynamicWillPayload);
    will.payloadLength = 0;
    will.qos = Esp32BaseMqtt::QOS_1;
    will.retain = true;
    g_dynamicWill = &will;

    Esp32BaseMqtt::ConnectionConfig config = validConfig();
    config.lastWill = &will;
    TEST_ASSERT_TRUE(Esp32BaseMqtt::configure(config));
    TEST_ASSERT_TRUE(
        Esp32BaseMqtt::setBeforeConnectCallback(beforeConnect));
    TEST_ASSERT_TRUE(Esp32BaseMqtt::begin());
    g_fakeWifiConnected = true;
    g_fakeRealTime = true;

    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(1, g_beforeConnectCount);
    TEST_ASSERT_EQUAL_STRING("offline-1", g_lastWillPayload);
    emitEvent(MQTT_EVENT_BEFORE_CONNECT);
    TEST_ASSERT_EQUAL(1, g_fakeSetConfigCount);
    TEST_ASSERT_EQUAL_STRING("offline-1", g_lastWillPayload);
    emitEvent(MQTT_EVENT_CONNECTED);
    Esp32BaseMqtt::handle(false);

    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(false);
    g_fakeMillis = g_mqttNextAttemptMs;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(2, g_beforeConnectCount);
    emitEvent(MQTT_EVENT_BEFORE_CONNECT);
    TEST_ASSERT_EQUAL(2, g_fakeSetConfigCount);
    TEST_ASSERT_EQUAL_STRING("offline-2", g_lastWillPayload);
}

void test_before_connect_output_is_revalidated() {
    Esp32BaseMqtt::LastWill will;
    will.topic = "device/availability";
    will.payload = reinterpret_cast<const uint8_t*>(g_dynamicWillPayload);
    will.payloadLength = 0;
    will.qos = Esp32BaseMqtt::QOS_1;
    will.retain = true;
    g_dynamicWill = &will;
    g_makeDynamicWillInvalid = true;

    Esp32BaseMqtt::ConnectionConfig config = validConfig();
    config.lastWill = &will;
    TEST_ASSERT_TRUE(Esp32BaseMqtt::configure(config));
    TEST_ASSERT_TRUE(
        Esp32BaseMqtt::setBeforeConnectCallback(beforeConnect));
    TEST_ASSERT_TRUE(Esp32BaseMqtt::begin());
    g_fakeWifiConnected = true;
    g_fakeRealTime = true;

    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONFIGURATION_ERROR,
                      Esp32BaseMqtt::state());
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::ERROR_INVALID_CONFIGURATION,
                      Esp32BaseMqtt::status().lastError);
    TEST_ASSERT_EQUAL_UINT32(
        1,
        Esp32BaseMqtt::diagnostics().connectionConfigurationFailures);
    TEST_ASSERT_EQUAL_UINT32(0,
                             Esp32BaseMqtt::diagnostics().connectAttempts);
    TEST_ASSERT_NULL(g_fakeEventHandler);
}

void test_native_before_connect_update_failure_is_terminal_until_retry() {
    startAndConnect();
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(false);
    g_fakeMillis = g_mqttNextAttemptMs;
    Esp32BaseMqtt::handle(false);
    for (uint8_t i = 0; i < ESP32BASE_MQTT_EVENT_SLOTS; ++i) {
        RawEvent queued;
        queued.type = RAW_PUBLISHED;
        queued.packetId = static_cast<int32_t>(100 + i);
        TEST_ASSERT_TRUE(pushRawEvent(queued));
    }
    const int subscriptionsBeforeFailure = g_fakeSubscribeCount;
    g_fakeSetConfigResult = ESP_ERR_NO_MEM;
    emitEvent(MQTT_EVENT_BEFORE_CONNECT);
    TEST_ASSERT_FALSE(isConnectionWanted());
    emitEvent(MQTT_EVENT_CONNECTED);
    TEST_ASSERT_EQUAL(subscriptionsBeforeFailure, g_fakeSubscribeCount);
    Esp32BaseMqtt::handle(false);

    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONFIGURATION_ERROR,
                      Esp32BaseMqtt::state());
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::ERROR_NO_MEMORY,
                      Esp32BaseMqtt::status().lastError);
    TEST_ASSERT_EQUAL_UINT32(
        1,
        Esp32BaseMqtt::diagnostics().connectionConfigurationFailures);
    TEST_ASSERT_EQUAL_UINT32(
        1,
        Esp32BaseMqtt::diagnostics().controlEventDropped);
    TEST_ASSERT_TRUE(Esp32BaseMqtt::requestReconnect());
    g_fakeSetConfigResult = ESP_OK;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::BACKOFF, Esp32BaseMqtt::state());
}

void test_incoming_mailbox_full_drops_whole_message() {
    Esp32BaseMqtt::setMessageCallback(onMessage);
    Esp32BaseMqtt::setEventCallback(onEvent);
    startAndConnect();
    g_applicationEventCount = 0;
    char topic[] = "device/command";
    char payload[] = "x";
    for (int packetId = 1; packetId <= 3; ++packetId) {
        esp_mqtt_event_t event = {};
        event.client = &g_fakeClient;
        event.event_id = MQTT_EVENT_DATA;
        event.msg_id = packetId;
        event.topic = topic;
        event.topic_len = strlen(topic);
        event.data = payload;
        event.data_len = 1;
        event.total_data_len = 1;
        event.current_data_offset = 0;
        g_fakeEventHandler(nullptr, nullptr, MQTT_EVENT_DATA, &event);
    }
    TEST_ASSERT_EQUAL(0, g_messageCount);
    TEST_ASSERT_EQUAL(1, Esp32BaseMqtt::diagnostics().incomingMailboxDropped);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(2, g_messageCount);
    TEST_ASSERT_EQUAL(1, g_applicationEventCount);
}

void test_connection_event_survives_full_control_mailbox() {
    startAndConnect();
    for (int i = 0; i < ESP32BASE_MQTT_EVENT_SLOTS; ++i) {
        emitEvent(MQTT_EVENT_PUBLISHED, 100 + i);
    }
    emitEvent(MQTT_EVENT_DISCONNECTED);
    TEST_ASSERT_EQUAL(1, Esp32BaseMqtt::diagnostics().controlEventDropped);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::BACKOFF, Esp32BaseMqtt::state());
}

void test_dns_and_certificate_errors_are_distinguished() {
    startAndConnect();
    esp_mqtt_error_codes_t dnsError = {};
    dnsError.error_type = MQTT_ERROR_TYPE_TCP_TRANSPORT;
    dnsError.esp_tls_last_esp_err =
        ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME;
    emitEvent(MQTT_EVENT_ERROR, 0, &dnsError);
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::ERROR_DNS_OR_TRANSPORT,
                      Esp32BaseMqtt::status().lastError);

    RawEvent certificateError;
    certificateError.type = RAW_ERROR;
    certificateError.certificateFlags = 1;
    bool terminal = false;
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::ERROR_TLS_CERTIFICATE,
                      classifyError(certificateError, terminal));
    TEST_ASSERT_TRUE(terminal);
}

void test_reports_platform_certificate_date_check_capability() {
    TEST_ASSERT_TRUE(Esp32BaseMqtt::configure(validConfig()));
    const Esp32BaseMqtt::Status status = Esp32BaseMqtt::status();
    TEST_ASSERT_TRUE(status.tls);
    TEST_ASSERT_FALSE(status.certificateDateCheckEnabled);
    TEST_ASSERT_EQUAL_STRING(
        "tls_certificate_date_check_unavailable",
        Esp32BaseMqtt::errorName(
            Esp32BaseMqtt::ERROR_TLS_CERTIFICATE_DATE_CHECK_UNAVAILABLE));
}

void test_certificate_error_without_flags_stops_automatic_retries() {
    startAndConnect();
    const int codes[] = {MBEDTLS_ERR_X509_CERT_VERIFY_FAILED,
                         -MBEDTLS_ERR_X509_CERT_VERIFY_FAILED};
    for (int code : codes) {
        RawEvent raw;
        raw.type = RAW_ERROR;
        raw.tlsError = code;
        bool terminal = false;
        TEST_ASSERT_EQUAL(Esp32BaseMqtt::ERROR_TLS_CERTIFICATE,
                          classifyError(raw, terminal));
        TEST_ASSERT_TRUE(terminal);
    }
    esp_mqtt_error_codes_t error = {};
    error.error_type = MQTT_ERROR_TYPE_TCP_TRANSPORT;
    error.esp_tls_stack_err = -MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    emitEvent(MQTT_EVENT_ERROR, 0, &error);
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::ERROR_TLS_CERTIFICATE,
                      Esp32BaseMqtt::status().lastError);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONNECTION_REJECTED, Esp32BaseMqtt::state());
    TEST_ASSERT_EQUAL(0, Esp32BaseMqtt::diagnostics().nativeCertificateFlags);
    const uint32_t attempts = Esp32BaseMqtt::diagnostics().connectAttempts;
    g_fakeMillis += 120000;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(attempts, Esp32BaseMqtt::diagnostics().connectAttempts);
    TEST_ASSERT_TRUE(Esp32BaseMqtt::requestReconnect());
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::BACKOFF, Esp32BaseMqtt::state());
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_GREATER_THAN(attempts, Esp32BaseMqtt::diagnostics().connectAttempts);
}

void test_secure_default_rejects_missing_certificate_date_check() {
    TEST_ASSERT_FALSE(Esp32BaseMqtt::configure(validConfig()));
    const Esp32BaseMqtt::Status status = Esp32BaseMqtt::status();
    TEST_ASSERT_FALSE(status.configured);
    TEST_ASSERT_FALSE(status.certificateDateCheckEnabled);
    TEST_ASSERT_EQUAL(
        Esp32BaseMqtt::ERROR_TLS_CERTIFICATE_DATE_CHECK_UNAVAILABLE,
        status.lastError);
}

void test_terminal_rejection_survives_wifi_loss_and_recovery() {
    TEST_ASSERT_TRUE(Esp32BaseMqtt::configure(validConfig()));
    TEST_ASSERT_TRUE(Esp32BaseMqtt::begin());
    g_fakeWifiConnected = true;
    g_fakeRealTime = true;
    Esp32BaseMqtt::handle(false);

    esp_mqtt_error_codes_t authError = {};
    authError.error_type = MQTT_ERROR_TYPE_CONNECTION_REFUSED;
    authError.connect_return_code = MQTT_CONNECTION_REFUSE_BAD_USERNAME;
    emitEvent(MQTT_EVENT_ERROR, 0, &authError);
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONNECTION_REJECTED,
                      Esp32BaseMqtt::state());

    g_fakeWifiConnected = false;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONNECTION_REJECTED,
                      Esp32BaseMqtt::state());
    g_fakeWifiConnected = true;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::CONNECTION_REJECTED,
                      Esp32BaseMqtt::state());
    TEST_ASSERT_EQUAL_UINT32(1, Esp32BaseMqtt::diagnostics().connectAttempts);
}

void test_shutdown_requires_matching_ack_and_disconnect_then_explicit_resume() {
    startAndConnect();
    Esp32BaseMqtt::PublishRequest finalMessage;
    finalMessage.topic = "device/availability";
    finalMessage.payload = reinterpret_cast<const uint8_t*>("offline");
    finalMessage.payloadLength = 7;
    finalMessage.qos = Esp32BaseMqtt::QOS_1;
    finalMessage.retain = true;
    TEST_ASSERT_TRUE(Esp32BaseMqtt::beginShutdown(finalMessage, 100));
    const int finalId = g_fakePacketId;
    TEST_ASSERT_FALSE(Esp32BaseMqtt::beginShutdown(finalMessage, 100));
    TEST_ASSERT_FALSE(Esp32BaseMqtt::publish(finalMessage).accepted());
    TEST_ASSERT_FALSE(Esp32BaseMqtt::requestReconnect());
    emitEvent(MQTT_EVENT_PUBLISHED, finalId + 1);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(0, g_fakeDisconnectCount);
    emitEvent(MQTT_EVENT_PUBLISHED, finalId);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(1, g_fakeDisconnectCount);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_IN_PROGRESS, Esp32BaseMqtt::shutdownResult());
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_SUCCESS, Esp32BaseMqtt::shutdownResult());
    g_fakeMillis += 10000;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::STOPPING, Esp32BaseMqtt::state());
    TEST_ASSERT_EQUAL(0, g_fakeStopCount);
    Esp32BaseMqtt::resumeAfterShutdown();
    TEST_ASSERT_FALSE(Esp32BaseMqtt::shutdownPaused());
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::BACKOFF, Esp32BaseMqtt::state());
}

void test_shutdown_timeout_preserves_lwt_and_failure_is_not_late_success() {
    startAndConnect();
    Esp32BaseMqtt::PublishRequest finalMessage;
    finalMessage.topic = "device/availability";
    finalMessage.qos = Esp32BaseMqtt::QOS_1;
    g_fakeMillis = UINT32_MAX - 20;
    TEST_ASSERT_TRUE(Esp32BaseMqtt::beginShutdown(finalMessage, 100));
    const int finalId = g_fakePacketId;
    g_fakeMillis += 99;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_IN_PROGRESS, Esp32BaseMqtt::shutdownResult());
    ++g_fakeMillis;
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_PUBACK_TIMEOUT, Esp32BaseMqtt::shutdownResult());
    TEST_ASSERT_EQUAL(0, g_fakeDisconnectCount);
    emitEvent(MQTT_EVENT_PUBLISHED, finalId);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(0, g_fakeDisconnectCount);
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_PUBACK_TIMEOUT, Esp32BaseMqtt::shutdownResult());
}

void test_shutdown_disconnect_timeout_and_mailbox_loss_are_not_success() {
    for (int lost = 0; lost < 2; ++lost) {
        resetModule();
        startAndConnect();
        Esp32BaseMqtt::PublishRequest finalMessage;
        finalMessage.topic = "device/availability";
        finalMessage.qos = Esp32BaseMqtt::QOS_1;
        TEST_ASSERT_TRUE(Esp32BaseMqtt::beginShutdown(finalMessage, 100));
        emitEvent(MQTT_EVENT_PUBLISHED, g_fakePacketId);
        if (lost) {
            ++g_mqttDiagnostics.controlEventDropped;
            emitEvent(MQTT_EVENT_DISCONNECTED);
        }
        Esp32BaseMqtt::handle(false);
        g_fakeMillis += 100;
        Esp32BaseMqtt::handle(false);
        TEST_ASSERT_EQUAL(lost ? Esp32BaseMqtt::SHUTDOWN_DELIVERY_UNCERTAIN
                               : Esp32BaseMqtt::SHUTDOWN_DISCONNECT_TIMEOUT,
                          Esp32BaseMqtt::shutdownResult());
    }
}

void test_shutdown_rejections_transport_loss_and_maintenance_failure() {
    Esp32BaseMqtt::PublishRequest finalMessage;
    finalMessage.topic = "device/availability";
    finalMessage.qos = Esp32BaseMqtt::QOS_1;
    TEST_ASSERT_FALSE(Esp32BaseMqtt::beginShutdown(finalMessage, 100));
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_NOT_CONNECTED, Esp32BaseMqtt::shutdownResult());
    startAndConnect();
    TEST_ASSERT_FALSE(Esp32BaseMqtt::beginShutdown(finalMessage, 0));
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_INVALID_REQUEST, Esp32BaseMqtt::shutdownResult());
    g_fakeEnqueueResult = -1;
    TEST_ASSERT_FALSE(Esp32BaseMqtt::beginShutdown(finalMessage, 100));
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_PUBLISH_FAILED, Esp32BaseMqtt::shutdownResult());
    TEST_ASSERT_FALSE(Esp32BaseMqtt::shutdownPaused());
    g_fakeEnqueueResult = 0;
    TEST_ASSERT_TRUE(Esp32BaseMqtt::beginShutdown(finalMessage, 100));
    emitEvent(MQTT_EVENT_DISCONNECTED);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_CONNECTION_LOST, Esp32BaseMqtt::shutdownResult());
    TEST_ASSERT_FALSE(Esp32BaseMqtt::settleShutdownForMaintenance(10));
    Esp32BaseMqtt::prepareForLifecycleStop();
    TEST_ASSERT_EQUAL(0, g_fakeDisconnectCount);

    resetModule(); startAndConnect();
    TEST_ASSERT_TRUE(Esp32BaseMqtt::beginShutdown(finalMessage, 100));
    g_fakeDisconnectResult = ESP_FAIL;
    emitEvent(MQTT_EVENT_PUBLISHED, g_fakePacketId);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_DISCONNECT_FAILED, Esp32BaseMqtt::shutdownResult());

    resetModule(); startAndConnect();
    TEST_ASSERT_TRUE(Esp32BaseMqtt::beginShutdown(finalMessage, 5000));
    TEST_ASSERT_FALSE(Esp32BaseMqtt::settleShutdownForMaintenance(10));
    TEST_ASSERT_EQUAL(Esp32BaseMqtt::SHUTDOWN_PUBACK_TIMEOUT, Esp32BaseMqtt::shutdownResult());
    esp32base_internal::suspendMqttForOta();
    Esp32BaseMqtt::prepareForLifecycleStop();
    TEST_ASSERT_EQUAL(0, g_fakeDisconnectCount);
    TEST_ASSERT_EQUAL(0, g_fakeStopCount);
}

void test_backoff_deadline_is_millis_wrap_safe() {
    TEST_ASSERT_TRUE(deadlineReached(3u, UINT32_MAX - 2u));
    TEST_ASSERT_FALSE(deadlineReached(UINT32_MAX - 3u, 2u));
}

} // namespace

void setUp() {
    resetModule();
}

void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
#if ESP32BASE_MQTT_EXPECT_DATE_CHECK_REJECTION
    RUN_TEST(test_secure_default_rejects_missing_certificate_date_check);
#else
    RUN_TEST(test_configuration_requires_ca_and_rejects_plaintext_by_default);
    RUN_TEST(test_begin_never_starts_network_and_waits_for_prerequisites);
    RUN_TEST(test_callbacks_are_deferred_until_handle_and_subscriptions_repeat);
    RUN_TEST(test_subscription_grant_survives_deferred_dispatch);
    RUN_TEST(test_qos1_publish_ack_and_outbox_limit);
    RUN_TEST(test_disconnect_discards_queued_and_partial_incoming_messages);
    RUN_TEST(test_ota_preparation_does_not_dispatch_application_callbacks);
    RUN_TEST(test_publish_capacity_does_not_expand_incoming_slots);
    RUN_TEST(test_fragment_assembly_and_oversize_drop);
    RUN_TEST(test_terminal_auth_rejection_requires_explicit_retry);
    RUN_TEST(test_lwt_is_mapped_and_ota_suspends_without_direct_callback);
    RUN_TEST(test_before_connect_refreshes_lwt_for_every_attempt);
    RUN_TEST(test_before_connect_output_is_revalidated);
    RUN_TEST(test_native_before_connect_update_failure_is_terminal_until_retry);
    RUN_TEST(test_incoming_mailbox_full_drops_whole_message);
    RUN_TEST(test_connection_event_survives_full_control_mailbox);
    RUN_TEST(test_dns_and_certificate_errors_are_distinguished);
    RUN_TEST(test_certificate_error_without_flags_stops_automatic_retries);
    RUN_TEST(test_reports_platform_certificate_date_check_capability);
    RUN_TEST(test_terminal_rejection_survives_wifi_loss_and_recovery);
    RUN_TEST(test_backoff_deadline_is_millis_wrap_safe);
    RUN_TEST(test_shutdown_requires_matching_ack_and_disconnect_then_explicit_resume);
    RUN_TEST(test_shutdown_timeout_preserves_lwt_and_failure_is_not_late_success);
    RUN_TEST(test_shutdown_disconnect_timeout_and_mailbox_loss_are_not_success);
    RUN_TEST(test_shutdown_rejections_transport_loss_and_maintenance_failure);
#endif
    return UNITY_END();
}
