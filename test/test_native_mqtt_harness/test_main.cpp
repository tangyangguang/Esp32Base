#include <unity.h>

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
    struct Snapshot {
        bool synced;
        uint32_t epochSec;
    };
    static bool isRealTime() { return g_fakeRealTime; }
    static Snapshot snapshot() { return {g_fakeRealTime, g_fakeRealTime ? 1800000000u : 0u}; }
};

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

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* config) {
    g_lastNativeConfig = *config;
    return &g_fakeClient;
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t) { return ESP_OK; }
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t) {
    ++g_fakeStopCount;
    return ESP_OK;
}
esp_err_t esp_mqtt_client_disconnect(esp_mqtt_client_handle_t) {
    ++g_fakeDisconnectCount;
    return ESP_OK;
}
esp_err_t esp_mqtt_client_reconnect(esp_mqtt_client_handle_t) { return ESP_OK; }
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t) { return ESP_OK; }

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
bool g_tryPublishOnUncertain = false;
Esp32BaseMqtt::PublishCode g_reentrantPublishCode =
    Esp32BaseMqtt::PUBLISH_ACCEPTED;
int g_messageCount = 0;
size_t g_lastPayloadLength = 0;
char g_lastTopic[ESP32BASE_MQTT_MAX_TOPIC_BYTES + 1] = {};

void onEvent(const Esp32BaseMqtt::Event& event, void*) {
    ++g_applicationEventCount;
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
    g_mqttEverConnected = false;
    g_mqttOtaSuspended = false;
    g_mqttReconnectRequested = false;
    g_mqttForceReconnectAfterDisconnect = false;
    g_mqttConnectionWanted = true;
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
    g_fakeWifiConnected = false;
    g_fakeRealTime = false;
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
    g_applicationEventCount = 0;
    g_tryPublishOnUncertain = false;
    g_reentrantPublishCode = Esp32BaseMqtt::PUBLISH_ACCEPTED;
    g_messageCount = 0;
    g_lastPayloadLength = 0;
    g_lastTopic[0] = '\0';
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
    emitEvent(MQTT_EVENT_CONNECTED);
    TEST_ASSERT_EQUAL(3, g_applicationEventCount);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(2, g_fakeSubscribeCount);
    TEST_ASSERT_TRUE(g_applicationEventCount >= 4);
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
    event.total_data_len = ESP32BASE_MQTT_MAX_PAYLOAD_BYTES + 1;
    event.current_data_offset = 0;
    g_fakeEventHandler(nullptr, nullptr, MQTT_EVENT_DATA, &event);
    Esp32BaseMqtt::handle(false);
    TEST_ASSERT_EQUAL(1, g_messageCount);
    TEST_ASSERT_EQUAL(1, Esp32BaseMqtt::diagnostics().incomingOversizeDropped);
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
    RUN_TEST(test_configuration_requires_ca_and_rejects_plaintext_by_default);
    RUN_TEST(test_begin_never_starts_network_and_waits_for_prerequisites);
    RUN_TEST(test_callbacks_are_deferred_until_handle_and_subscriptions_repeat);
    RUN_TEST(test_qos1_publish_ack_and_outbox_limit);
    RUN_TEST(test_fragment_assembly_and_oversize_drop);
    RUN_TEST(test_terminal_auth_rejection_requires_explicit_retry);
    RUN_TEST(test_lwt_is_mapped_and_ota_suspends_without_direct_callback);
    RUN_TEST(test_incoming_mailbox_full_drops_whole_message);
    RUN_TEST(test_connection_event_survives_full_control_mailbox);
    RUN_TEST(test_dns_and_certificate_errors_are_distinguished);
    RUN_TEST(test_backoff_deadline_is_millis_wrap_safe);
    return UNITY_END();
}
