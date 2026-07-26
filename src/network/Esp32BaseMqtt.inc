#include <esp_idf_version.h>
#include <esp_system.h>
#include <esp_tls_errors.h>
#include <mqtt_client.h>

#include <errno.h>
#include <string.h>

namespace {

constexpr uint16_t MQTT_MIN_KEEPALIVE_SECONDS = 15;
constexpr uint16_t MQTT_MAX_KEEPALIVE_SECONDS = 1200;
constexpr size_t MQTT_MAX_HOST_BYTES = 253;
constexpr size_t MQTT_MAX_CLIENT_ID_BYTES = 64;
constexpr size_t MQTT_MAX_USERNAME_BYTES = 128;
constexpr size_t MQTT_MAX_PASSWORD_BYTES = 256;
constexpr uint32_t MQTT_BACKOFF_INITIAL_MS = 1000;
constexpr uint32_t MQTT_BACKOFF_MAX_MS = 60000;
constexpr uint8_t MQTT_NO_SLOT = 0xFF;

enum RawEventType : uint8_t {
    RAW_CONNECTED,
    RAW_DISCONNECTED,
    RAW_ERROR,
    RAW_SUBSCRIBED,
    RAW_PUBLISHED
};

struct RawEvent {
    RawEventType type = RAW_DISCONNECTED;
    int32_t packetId = 0;
    bool subscriptionRejected = false;
    int32_t errorType = 0;
    int32_t connectReturnCode = 0;
    int32_t espError = 0;
    int32_t tlsError = 0;
    int32_t socketError = 0;
    uint32_t certificateFlags = 0;
};

enum IncomingSlotState : uint8_t {
    INCOMING_EMPTY,
    INCOMING_ASSEMBLING,
    INCOMING_READY,
    INCOMING_DISPATCHING
};

struct IncomingSlot {
    IncomingSlotState state = INCOMING_EMPTY;
    int32_t packetId = 0;
    char topic[ESP32BASE_MQTT_MAX_TOPIC_BYTES + 1] = {};
    uint16_t topicLength = 0;
    uint16_t payloadLength = 0;
    Esp32BaseMqtt::QualityOfService qos = Esp32BaseMqtt::QOS_0;
    bool retain = false;
    bool duplicate = false;
    uint8_t payload[ESP32BASE_MQTT_MAX_PAYLOAD_BYTES] = {};
};

struct SubscriptionEntry {
    const char* topicFilter = nullptr;
    Esp32BaseMqtt::QualityOfService qos = Esp32BaseMqtt::QOS_0;
    int32_t pendingPacketId = -1;
};

struct InflightEntry {
    bool used = false;
    uint16_t packetId = 0;
};

portMUX_TYPE g_mqttMux = portMUX_INITIALIZER_UNLOCKED;
Esp32BaseMqtt::ConnectionConfig g_mqttConfig;
SubscriptionEntry g_mqttSubscriptions[ESP32BASE_MQTT_MAX_SUBSCRIPTIONS];
uint8_t g_mqttSubscriptionCount = 0;
InflightEntry g_mqttInflight[ESP32BASE_MQTT_MAX_INFLIGHT_QOS1];
IncomingSlot g_mqttIncoming[ESP32BASE_MQTT_INCOMING_SLOTS];
RawEvent g_mqttEvents[ESP32BASE_MQTT_EVENT_SLOTS];
uint8_t g_mqttEventRead = 0;
uint8_t g_mqttEventWrite = 0;
uint8_t g_mqttEventCount = 0;
int8_t g_mqttAssemblySlot = -1;
int32_t g_mqttDroppingPacketId = -1;
bool g_mqttConfigured = false;
bool g_mqttConfigurationAttempted = false;
bool g_mqttBegun = false;
bool g_mqttTerminalRejection = false;
bool g_mqttEverConnected = false;
bool g_mqttOtaSuspended = false;
bool g_mqttReconnectRequested = false;
bool g_mqttForceReconnectAfterDisconnect = false;
bool g_mqttConnectionWanted = true;
esp_mqtt_client_handle_t g_mqttClient = nullptr;
Esp32BaseMqtt::State g_mqttState = Esp32BaseMqtt::NOT_CONFIGURED;
Esp32BaseMqtt::Error g_mqttLastError = Esp32BaseMqtt::ERROR_NONE;
Esp32BaseMqtt::Error g_mqttPendingDisconnectError =
    Esp32BaseMqtt::ERROR_DISCONNECTED;
Esp32BaseMqtt::Diagnostics g_mqttDiagnostics;
uint32_t g_mqttNextAttemptMs = 0;
uint8_t g_mqttBackoffStep = 0;
uint32_t g_mqttReportedMailboxDrops = 0;
uint32_t g_mqttReportedIncomingDrops = 0;
uint32_t g_mqttConnectedAtUptimeMs = 0;
uint32_t g_mqttConnectedAtEpochSec = 0;
Esp32BaseMqtt::MessageCallback g_mqttMessageCallback = nullptr;
void* g_mqttMessageContext = nullptr;
Esp32BaseMqtt::EventCallback g_mqttEventCallback = nullptr;
void* g_mqttEventContext = nullptr;

size_t boundedLength(const char* value, size_t maximum) {
    if (!value) {
        return 0;
    }
    size_t length = 0;
    while (length <= maximum && value[length] != '\0') {
        ++length;
    }
    return length;
}

bool validRequiredString(const char* value, size_t maximum) {
    const size_t length = boundedLength(value, maximum);
    return length > 0 && length <= maximum;
}

bool validOptionalString(const char* value, size_t maximum) {
    return !value || boundedLength(value, maximum) <= maximum;
}

bool validPublishTopic(const char* topic) {
    if (!validRequiredString(topic, ESP32BASE_MQTT_MAX_TOPIC_BYTES)) {
        return false;
    }
    return strchr(topic, '#') == nullptr && strchr(topic, '+') == nullptr;
}

bool validTopicFilter(const char* filter) {
    if (!validRequiredString(filter, ESP32BASE_MQTT_MAX_TOPIC_BYTES)) {
        return false;
    }
    for (size_t i = 0; filter[i] != '\0'; ++i) {
        if (filter[i] == '#') {
            if (filter[i + 1] != '\0' || (i > 0 && filter[i - 1] != '/')) {
                return false;
            }
        } else if (filter[i] == '+') {
            const bool startsLevel = i == 0 || filter[i - 1] == '/';
            const bool endsLevel = filter[i + 1] == '\0' || filter[i + 1] == '/';
            if (!startsLevel || !endsLevel) {
                return false;
            }
        }
    }
    return true;
}

bool validPem(const char* pem, size_t length, size_t maximum) {
    if (!pem || length < 2 || length > maximum) {
        return false;
    }
    const size_t visibleLength = boundedLength(pem, maximum - 1);
    return visibleLength > 0 &&
           visibleLength + 1 == length &&
           strcmp(pem, "NULL") != 0;
}

bool validCertificatePem(const char* pem, size_t length, size_t maximum) {
    return validPem(pem, length, maximum) &&
           strstr(pem, "-----BEGIN CERTIFICATE-----") != nullptr &&
           strstr(pem, "-----END CERTIFICATE-----") != nullptr;
}

bool deadlineReached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

uint8_t incomingUsedLocked() {
    uint8_t used = 0;
    for (uint8_t i = 0; i < ESP32BASE_MQTT_INCOMING_SLOTS; ++i) {
        if (g_mqttIncoming[i].state != INCOMING_EMPTY) {
            ++used;
        }
    }
    return used;
}

void updateIncomingHighWaterLocked() {
    const uint8_t used = incomingUsedLocked();
    if (used > g_mqttDiagnostics.incomingHighWater) {
        g_mqttDiagnostics.incomingHighWater = used;
    }
}

bool pushRawEvent(const RawEvent& event) {
    bool accepted = false;
    portENTER_CRITICAL(&g_mqttMux);
    if (g_mqttEventCount < ESP32BASE_MQTT_EVENT_SLOTS) {
        g_mqttEvents[g_mqttEventWrite] = event;
        g_mqttEventWrite = static_cast<uint8_t>((g_mqttEventWrite + 1) % ESP32BASE_MQTT_EVENT_SLOTS);
        ++g_mqttEventCount;
        if (g_mqttEventCount > g_mqttDiagnostics.controlEventHighWater) {
            g_mqttDiagnostics.controlEventHighWater = g_mqttEventCount;
        }
        accepted = true;
    } else if (event.type == RAW_CONNECTED ||
               event.type == RAW_DISCONNECTED ||
               event.type == RAW_ERROR) {
        // Preserve connection truth even under an ACK burst. Drop the oldest
        // lower-priority event and retain the newest state/error event.
        g_mqttEvents[g_mqttEventWrite] = event;
        g_mqttEventWrite =
            static_cast<uint8_t>((g_mqttEventWrite + 1) % ESP32BASE_MQTT_EVENT_SLOTS);
        g_mqttEventRead = g_mqttEventWrite;
        ++g_mqttDiagnostics.controlEventDropped;
        accepted = true;
    } else {
        ++g_mqttDiagnostics.controlEventDropped;
    }
    portEXIT_CRITICAL(&g_mqttMux);
    return accepted;
}

bool popRawEvent(RawEvent& event) {
    bool available = false;
    portENTER_CRITICAL(&g_mqttMux);
    if (g_mqttEventCount > 0) {
        event = g_mqttEvents[g_mqttEventRead];
        g_mqttEventRead = static_cast<uint8_t>((g_mqttEventRead + 1) % ESP32BASE_MQTT_EVENT_SLOTS);
        --g_mqttEventCount;
        available = true;
    }
    portEXIT_CRITICAL(&g_mqttMux);
    return available;
}

void emitApplicationEvent(Esp32BaseMqtt::EventType type,
                          Esp32BaseMqtt::Error error = Esp32BaseMqtt::ERROR_NONE,
                          uint16_t packetId = 0,
                          uint8_t subscriptionIndex = MQTT_NO_SLOT) {
    if (!g_mqttEventCallback) {
        return;
    }
    Esp32BaseMqtt::Event event;
    event.type = type;
    event.error = error;
    event.packetId = packetId;
    event.subscriptionIndex = subscriptionIndex;
    g_mqttEventCallback(event, g_mqttEventContext);
}

uint8_t reserveIncomingSlot(int32_t packetId) {
    uint8_t selected = MQTT_NO_SLOT;
    portENTER_CRITICAL(&g_mqttMux);
    for (uint8_t i = 0; i < ESP32BASE_MQTT_INCOMING_SLOTS; ++i) {
        if (g_mqttIncoming[i].state == INCOMING_EMPTY) {
            g_mqttIncoming[i].state = INCOMING_ASSEMBLING;
            g_mqttIncoming[i].packetId = packetId;
            selected = i;
            updateIncomingHighWaterLocked();
            break;
        }
    }
    if (selected == MQTT_NO_SLOT) {
        ++g_mqttDiagnostics.incomingMailboxDropped;
    }
    portEXIT_CRITICAL(&g_mqttMux);
    return selected;
}

void abandonAssembly() {
    portENTER_CRITICAL(&g_mqttMux);
    if (g_mqttAssemblySlot >= 0 &&
        g_mqttIncoming[g_mqttAssemblySlot].state == INCOMING_ASSEMBLING) {
        g_mqttIncoming[g_mqttAssemblySlot].state = INCOMING_EMPTY;
    }
    g_mqttAssemblySlot = -1;
    portEXIT_CRITICAL(&g_mqttMux);
}

void finishDroppingIfComplete(const esp_mqtt_event_t* event) {
    if (event->current_data_offset + event->data_len >= event->total_data_len) {
        g_mqttDroppingPacketId = -1;
    }
}

void handleIncomingData(const esp_mqtt_event_t* event) {
    if (!event || event->data_len < 0 || event->total_data_len < 0 ||
        event->current_data_offset < 0 ||
        event->current_data_offset + event->data_len > event->total_data_len) {
        portENTER_CRITICAL(&g_mqttMux);
        ++g_mqttDiagnostics.incomingOversizeDropped;
        portEXIT_CRITICAL(&g_mqttMux);
        abandonAssembly();
        return;
    }

    if (g_mqttDroppingPacketId == event->msg_id) {
        finishDroppingIfComplete(event);
        return;
    }

    if (event->current_data_offset == 0) {
        abandonAssembly();
        if (!event->topic || event->topic_len <= 0 ||
            event->topic_len > ESP32BASE_MQTT_MAX_TOPIC_BYTES ||
            memchr(event->topic, '\0', static_cast<size_t>(event->topic_len)) != nullptr ||
            event->total_data_len > ESP32BASE_MQTT_MAX_PAYLOAD_BYTES) {
            portENTER_CRITICAL(&g_mqttMux);
            ++g_mqttDiagnostics.incomingOversizeDropped;
            portEXIT_CRITICAL(&g_mqttMux);
            g_mqttDroppingPacketId = event->msg_id;
            finishDroppingIfComplete(event);
            return;
        }

        const uint8_t slotIndex = reserveIncomingSlot(event->msg_id);
        if (slotIndex == MQTT_NO_SLOT) {
            g_mqttDroppingPacketId = event->msg_id;
            finishDroppingIfComplete(event);
            return;
        }

        IncomingSlot& slot = g_mqttIncoming[slotIndex];
        memcpy(slot.topic, event->topic, static_cast<size_t>(event->topic_len));
        slot.topic[event->topic_len] = '\0';
        slot.topicLength = static_cast<uint16_t>(event->topic_len);
        slot.payloadLength = static_cast<uint16_t>(event->total_data_len);
        slot.qos = event->qos == 1 ? Esp32BaseMqtt::QOS_1 : Esp32BaseMqtt::QOS_0;
        slot.retain = event->retain;
        slot.duplicate = event->dup;
        g_mqttAssemblySlot = static_cast<int8_t>(slotIndex);
    }

    if (g_mqttAssemblySlot < 0) {
        portENTER_CRITICAL(&g_mqttMux);
        ++g_mqttDiagnostics.incomingOversizeDropped;
        portEXIT_CRITICAL(&g_mqttMux);
        g_mqttDroppingPacketId = event->msg_id;
        finishDroppingIfComplete(event);
        return;
    }

    IncomingSlot& slot = g_mqttIncoming[g_mqttAssemblySlot];
    if (slot.packetId != event->msg_id ||
        event->current_data_offset + event->data_len > slot.payloadLength) {
        portENTER_CRITICAL(&g_mqttMux);
        ++g_mqttDiagnostics.incomingOversizeDropped;
        portEXIT_CRITICAL(&g_mqttMux);
        abandonAssembly();
        g_mqttDroppingPacketId = event->msg_id;
        finishDroppingIfComplete(event);
        return;
    }

    if (event->data_len > 0) {
        memcpy(slot.payload + event->current_data_offset,
               event->data,
               static_cast<size_t>(event->data_len));
    }
    if (event->current_data_offset + event->data_len == event->total_data_len) {
        portENTER_CRITICAL(&g_mqttMux);
        slot.state = INCOMING_READY;
        ++g_mqttDiagnostics.receivedMessages;
        g_mqttDiagnostics.receivedBytes += static_cast<uint32_t>(slot.payloadLength);
        g_mqttAssemblySlot = -1;
        portEXIT_CRITICAL(&g_mqttMux);
    }
}

void submitSubscriptionsFromMqttTask(esp_mqtt_client_handle_t client) {
    for (uint8_t i = 0; i < g_mqttSubscriptionCount; ++i) {
        const int packetId = esp_mqtt_client_subscribe(
            client,
            g_mqttSubscriptions[i].topicFilter,
            static_cast<int>(g_mqttSubscriptions[i].qos));
        portENTER_CRITICAL(&g_mqttMux);
        g_mqttSubscriptions[i].pendingPacketId = packetId;
        portEXIT_CRITICAL(&g_mqttMux);
        if (packetId < 0) {
            RawEvent rejected;
            rejected.type = RAW_SUBSCRIBED;
            rejected.packetId = -1 - static_cast<int32_t>(i);
            pushRawEvent(rejected);
        }
    }
}

void mqttEventHandler(void*, esp_event_base_t, int32_t eventId, void* eventData) {
    esp_mqtt_event_t* event = static_cast<esp_mqtt_event_t*>(eventData);
    if (!event) {
        return;
    }

    if (eventId == MQTT_EVENT_DATA) {
        handleIncomingData(event);
        return;
    }

    RawEvent raw;
    switch (eventId) {
        case MQTT_EVENT_CONNECTED: {
            portENTER_CRITICAL(&g_mqttMux);
            const bool connectionWanted = g_mqttConnectionWanted;
            portEXIT_CRITICAL(&g_mqttMux);
            if (!connectionWanted) {
                esp_mqtt_client_disconnect(event->client);
                return;
            }
            submitSubscriptionsFromMqttTask(event->client);
            raw.type = RAW_CONNECTED;
            pushRawEvent(raw);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            raw.type = RAW_DISCONNECTED;
            pushRawEvent(raw);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            raw.type = RAW_SUBSCRIBED;
            raw.packetId = event->msg_id;
            for (int i = 0; event->data && i < event->data_len; ++i) {
                if (static_cast<uint8_t>(event->data[i]) >= 0x80) {
                    raw.subscriptionRejected = true;
                    break;
                }
            }
            pushRawEvent(raw);
            break;
        case MQTT_EVENT_PUBLISHED:
            raw.type = RAW_PUBLISHED;
            raw.packetId = event->msg_id;
            pushRawEvent(raw);
            break;
        case MQTT_EVENT_ERROR:
            raw.type = RAW_ERROR;
            if (event->error_handle) {
                raw.errorType = event->error_handle->error_type;
                raw.connectReturnCode = event->error_handle->connect_return_code;
                raw.espError = event->error_handle->esp_tls_last_esp_err;
                raw.tlsError = event->error_handle->esp_tls_stack_err;
                raw.socketError = event->error_handle->esp_transport_sock_errno;
                raw.certificateFlags =
                    static_cast<uint32_t>(event->error_handle->esp_tls_cert_verify_flags);
            }
            pushRawEvent(raw);
            break;
        default:
            break;
    }
}

void fillNativeConfig(esp_mqtt_client_config_t& nativeConfig) {
    memset(&nativeConfig, 0, sizeof(nativeConfig));
    const bool tls = g_mqttConfig.security == Esp32BaseMqtt::TLS;
#if ESP_IDF_VERSION_MAJOR >= 5
    nativeConfig.broker.address.hostname = g_mqttConfig.host;
    nativeConfig.broker.address.port = g_mqttConfig.port;
    nativeConfig.broker.address.transport =
        tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
    nativeConfig.broker.verification.certificate =
        tls ? g_mqttConfig.tls.caCertificatePem : nullptr;
    nativeConfig.broker.verification.certificate_len = 0;
    nativeConfig.broker.verification.skip_cert_common_name_check = false;
    nativeConfig.credentials.client_id = g_mqttConfig.clientId;
    nativeConfig.credentials.username = g_mqttConfig.username;
    nativeConfig.credentials.authentication.password = g_mqttConfig.password;
    nativeConfig.credentials.authentication.certificate =
        tls ? g_mqttConfig.tls.clientCertificatePem : nullptr;
    nativeConfig.credentials.authentication.certificate_len = 0;
    nativeConfig.credentials.authentication.key =
        tls ? g_mqttConfig.tls.privateKeyPem : nullptr;
    nativeConfig.credentials.authentication.key_len = 0;
    nativeConfig.session.disable_clean_session = false;
    nativeConfig.session.keepalive = g_mqttConfig.keepAliveSeconds;
    nativeConfig.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    if (g_mqttConfig.lastWill) {
        nativeConfig.session.last_will.topic = g_mqttConfig.lastWill->topic;
        nativeConfig.session.last_will.msg =
            reinterpret_cast<const char*>(g_mqttConfig.lastWill->payload);
        nativeConfig.session.last_will.msg_len =
            static_cast<int>(g_mqttConfig.lastWill->payloadLength);
        nativeConfig.session.last_will.qos = static_cast<int>(g_mqttConfig.lastWill->qos);
        nativeConfig.session.last_will.retain = g_mqttConfig.lastWill->retain ? 1 : 0;
    }
    nativeConfig.network.disable_auto_reconnect = true;
    nativeConfig.network.timeout_ms = 10000;
    nativeConfig.task.stack_size = ESP32BASE_MQTT_TASK_STACK_BYTES;
    nativeConfig.task.priority = 5;
    nativeConfig.buffer.size = 1024;
    nativeConfig.buffer.out_size = 1024;
    nativeConfig.outbox.limit = ESP32BASE_MQTT_MAX_OUTBOX_BYTES;
#else
    nativeConfig.host = g_mqttConfig.host;
    nativeConfig.port = g_mqttConfig.port;
    nativeConfig.transport = tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
    nativeConfig.cert_pem = tls ? g_mqttConfig.tls.caCertificatePem : nullptr;
    nativeConfig.cert_len = 0;
    nativeConfig.skip_cert_common_name_check = false;
    nativeConfig.client_id = g_mqttConfig.clientId;
    nativeConfig.username = g_mqttConfig.username;
    nativeConfig.password = g_mqttConfig.password;
    nativeConfig.client_cert_pem =
        tls ? g_mqttConfig.tls.clientCertificatePem : nullptr;
    nativeConfig.client_cert_len = 0;
    nativeConfig.client_key_pem =
        tls ? g_mqttConfig.tls.privateKeyPem : nullptr;
    nativeConfig.client_key_len = 0;
    nativeConfig.disable_clean_session = false;
    nativeConfig.keepalive = g_mqttConfig.keepAliveSeconds;
    nativeConfig.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    if (g_mqttConfig.lastWill) {
        nativeConfig.lwt_topic = g_mqttConfig.lastWill->topic;
        nativeConfig.lwt_msg = reinterpret_cast<const char*>(g_mqttConfig.lastWill->payload);
        nativeConfig.lwt_msg_len = static_cast<int>(g_mqttConfig.lastWill->payloadLength);
        nativeConfig.lwt_qos = static_cast<int>(g_mqttConfig.lastWill->qos);
        nativeConfig.lwt_retain = g_mqttConfig.lastWill->retain ? 1 : 0;
    }
    nativeConfig.disable_auto_reconnect = true;
    nativeConfig.network_timeout_ms = 10000;
    nativeConfig.task_stack = ESP32BASE_MQTT_TASK_STACK_BYTES;
    nativeConfig.task_prio = 5;
    nativeConfig.buffer_size = 1024;
    nativeConfig.out_buffer_size = 1024;
#endif
}

void clearInflight(bool notifyUncertain) {
    for (uint8_t i = 0; i < ESP32BASE_MQTT_MAX_INFLIGHT_QOS1; ++i) {
        uint16_t packetId = 0;
        portENTER_CRITICAL(&g_mqttMux);
        if (g_mqttInflight[i].used) {
            packetId = g_mqttInflight[i].packetId;
            g_mqttInflight[i].used = false;
            if (notifyUncertain) {
                ++g_mqttDiagnostics.publishDeliveryUncertain;
            }
        }
        portEXIT_CRITICAL(&g_mqttMux);
        if (packetId != 0 && notifyUncertain) {
            emitApplicationEvent(Esp32BaseMqtt::EVENT_PUBLISH_DELIVERY_UNCERTAIN,
                                 Esp32BaseMqtt::ERROR_DISCONNECTED,
                                 packetId);
        }
    }
}

void destroyUnstartedMqttClient() {
    if (!g_mqttClient) {
        return;
    }
    esp_mqtt_client_destroy(g_mqttClient);
    g_mqttClient = nullptr;
}

void requestNativeDisconnect() {
    portENTER_CRITICAL(&g_mqttMux);
    g_mqttConnectionWanted = false;
    portEXIT_CRITICAL(&g_mqttMux);
    if (g_mqttClient) {
        // This only sets ESP-MQTT's disconnect bit and returns. Never call
        // esp_mqtt_client_stop() from handle(): that API may wait forever.
        esp_mqtt_client_disconnect(g_mqttClient);
    }
}

bool isConnectionWanted() {
    portENTER_CRITICAL(&g_mqttMux);
    const bool wanted = g_mqttConnectionWanted;
    portEXIT_CRITICAL(&g_mqttMux);
    return wanted;
}

void setState(Esp32BaseMqtt::State state) {
    portENTER_CRITICAL(&g_mqttMux);
    g_mqttState = state;
    portEXIT_CRITICAL(&g_mqttMux);
}

void setLastError(Esp32BaseMqtt::Error error) {
    portENTER_CRITICAL(&g_mqttMux);
    g_mqttLastError = error;
    portEXIT_CRITICAL(&g_mqttMux);
}

void scheduleBackoff() {
    const uint8_t shift = g_mqttBackoffStep > 6 ? 6 : g_mqttBackoffStep;
    uint32_t ceiling = MQTT_BACKOFF_INITIAL_MS << shift;
    if (ceiling > MQTT_BACKOFF_MAX_MS) {
        ceiling = MQTT_BACKOFF_MAX_MS;
    }
    const uint32_t floor = ceiling / 2;
    const uint32_t span = ceiling - floor + 1;
    const uint32_t delayMs = floor + (esp_random() % span);
    g_mqttNextAttemptMs = millis() + delayMs;
    if (g_mqttBackoffStep < 7) {
        ++g_mqttBackoffStep;
    }
    setState(Esp32BaseMqtt::BACKOFF);
}

Esp32BaseMqtt::Error classifyError(const RawEvent& raw, bool& terminal) {
    terminal = false;
    if (raw.errorType == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        switch (raw.connectReturnCode) {
            case MQTT_CONNECTION_REFUSE_PROTOCOL:
                terminal = true;
                return Esp32BaseMqtt::ERROR_PROTOCOL_REJECTED;
            case MQTT_CONNECTION_REFUSE_ID_REJECTED:
                terminal = true;
                return Esp32BaseMqtt::ERROR_CLIENT_ID_REJECTED;
            case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
                terminal = true;
                return Esp32BaseMqtt::ERROR_BAD_CREDENTIALS;
            case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
                terminal = true;
                return Esp32BaseMqtt::ERROR_NOT_AUTHORIZED;
            case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
                return Esp32BaseMqtt::ERROR_BROKER_UNAVAILABLE;
            default:
                return Esp32BaseMqtt::ERROR_DNS_OR_TRANSPORT;
        }
    }
    if (raw.certificateFlags != 0) {
        terminal = true;
        return Esp32BaseMqtt::ERROR_TLS_CERTIFICATE;
    }
    if (raw.espError == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME ||
        raw.espError == ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET ||
        raw.espError == ESP_ERR_ESP_TLS_UNSUPPORTED_PROTOCOL_FAMILY ||
        raw.espError == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST ||
        raw.espError == ESP_ERR_ESP_TLS_SOCKET_SETOPT_FAILED ||
        raw.espError == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT
#ifdef ESP_ERR_ESP_TLS_TCP_CLOSED_FIN
        || raw.espError == ESP_ERR_ESP_TLS_TCP_CLOSED_FIN
#endif
    ) {
        return Esp32BaseMqtt::ERROR_DNS_OR_TRANSPORT;
    }
    if (g_mqttConfig.security == Esp32BaseMqtt::TLS &&
        (raw.tlsError != 0 || raw.espError != 0)) {
        return Esp32BaseMqtt::ERROR_TLS;
    }
    return Esp32BaseMqtt::ERROR_DNS_OR_TRANSPORT;
}

void dispatchMailboxOverflowIfNeeded() {
    uint32_t current = 0;
    portENTER_CRITICAL(&g_mqttMux);
    current = g_mqttDiagnostics.controlEventDropped;
    portEXIT_CRITICAL(&g_mqttMux);
    if (current != g_mqttReportedMailboxDrops) {
        g_mqttReportedMailboxDrops = current;
        emitApplicationEvent(Esp32BaseMqtt::EVENT_MAILBOX_OVERFLOW);
    }
}

void dispatchIncomingDropIfNeeded() {
    uint32_t current = 0;
    portENTER_CRITICAL(&g_mqttMux);
    current = g_mqttDiagnostics.incomingOversizeDropped +
              g_mqttDiagnostics.incomingMailboxDropped;
    portEXIT_CRITICAL(&g_mqttMux);
    if (current != g_mqttReportedIncomingDrops) {
        g_mqttReportedIncomingDrops = current;
        emitApplicationEvent(Esp32BaseMqtt::EVENT_INCOMING_MESSAGE_DROPPED);
    }
}

void handleRawEvents() {
    RawEvent raw;
    while (popRawEvent(raw)) {
        switch (raw.type) {
            case RAW_CONNECTED: {
                if (g_mqttOtaSuspended || !isConnectionWanted()) {
                    requestNativeDisconnect();
                    setState(g_mqttOtaSuspended
                                 ? Esp32BaseMqtt::SUSPENDED_FOR_OTA
                                 : Esp32BaseMqtt::BACKOFF);
                    break;
                }
                const bool reconnect = g_mqttEverConnected;
                g_mqttEverConnected = true;
                g_mqttTerminalRejection = false;
                g_mqttBackoffStep = 0;
                g_mqttPendingDisconnectError =
                    Esp32BaseMqtt::ERROR_DISCONNECTED;
                g_mqttConnectedAtUptimeMs = millis();
                const Esp32BaseTime::Snapshot snapshot = Esp32BaseTime::snapshot();
                g_mqttConnectedAtEpochSec = snapshot.synced ? snapshot.epochSec : 0;
                portENTER_CRITICAL(&g_mqttMux);
                ++g_mqttDiagnostics.successfulConnections;
                if (reconnect) {
                    ++g_mqttDiagnostics.reconnects;
                }
                g_mqttLastError = Esp32BaseMqtt::ERROR_NONE;
                portEXIT_CRITICAL(&g_mqttMux);
                setState(Esp32BaseMqtt::CONNECTED);
                ESP32BASE_LOG_I("mqtt",
                                "connected host=%s port=%u client_id=%s tls=%s",
                                g_mqttConfig.host,
                                static_cast<unsigned>(g_mqttConfig.port),
                                g_mqttConfig.clientId,
                                g_mqttConfig.security == Esp32BaseMqtt::TLS ? "true" : "false");
                emitApplicationEvent(Esp32BaseMqtt::EVENT_CONNECTED);
                break;
            }
            case RAW_ERROR: {
                bool terminal = false;
                const Esp32BaseMqtt::Error error = classifyError(raw, terminal);
                portENTER_CRITICAL(&g_mqttMux);
                g_mqttLastError = error;
                g_mqttPendingDisconnectError = error;
                g_mqttDiagnostics.nativeEspError = raw.espError;
                g_mqttDiagnostics.nativeTlsError = raw.tlsError;
                g_mqttDiagnostics.nativeSocketError = raw.socketError;
                g_mqttDiagnostics.nativeCertificateFlags = raw.certificateFlags;
                portEXIT_CRITICAL(&g_mqttMux);
                g_mqttTerminalRejection = terminal;
                ESP32BASE_LOG_W("mqtt",
                                "connection_error host=%s port=%u error=%s esp=%ld tls=%ld socket=%ld cert_flags=%lu credential_set=%s",
                                g_mqttConfig.host,
                                static_cast<unsigned>(g_mqttConfig.port),
                                Esp32BaseMqtt::errorName(error),
                                static_cast<long>(raw.espError),
                                static_cast<long>(raw.tlsError),
                                static_cast<long>(raw.socketError),
                                static_cast<unsigned long>(raw.certificateFlags),
                                g_mqttConfig.username ? "true" : "false");
                if (terminal) {
                    emitApplicationEvent(Esp32BaseMqtt::EVENT_CONNECTION_REJECTED, error);
                }
                break;
            }
            case RAW_DISCONNECTED: {
                portENTER_CRITICAL(&g_mqttMux);
                g_mqttLastError = g_mqttPendingDisconnectError;
                const Esp32BaseMqtt::Error disconnectError =
                    g_mqttPendingDisconnectError;
                g_mqttPendingDisconnectError =
                    Esp32BaseMqtt::ERROR_DISCONNECTED;
                ++g_mqttDiagnostics.disconnects;
                portEXIT_CRITICAL(&g_mqttMux);
                // Reject re-entrant publish() calls from the events below
                // before refining the prerequisite-specific waiting state.
                setState(Esp32BaseMqtt::BACKOFF);
                clearInflight(true);
                emitApplicationEvent(Esp32BaseMqtt::EVENT_DISCONNECTED,
                                     disconnectError);
                if (g_mqttForceReconnectAfterDisconnect) {
                    g_mqttForceReconnectAfterDisconnect = false;
                    portENTER_CRITICAL(&g_mqttMux);
                    g_mqttConnectionWanted = true;
                    portEXIT_CRITICAL(&g_mqttMux);
                    g_mqttBackoffStep = 0;
                    g_mqttNextAttemptMs = millis();
                    setState(Esp32BaseMqtt::BACKOFF);
                } else if (g_mqttTerminalRejection) {
                    setState(Esp32BaseMqtt::CONNECTION_REJECTED);
                } else if (g_mqttOtaSuspended) {
                    setState(Esp32BaseMqtt::SUSPENDED_FOR_OTA);
                } else if (!Esp32BaseWiFi::isConnected()) {
                    setState(Esp32BaseMqtt::WAITING_FOR_WIFI);
                } else if (g_mqttConfig.security == Esp32BaseMqtt::TLS &&
                           !Esp32BaseTime::isRealTime()) {
                    setState(Esp32BaseMqtt::WAITING_FOR_TIME);
                } else if (!isConnectionWanted()) {
                    setState(Esp32BaseMqtt::BACKOFF);
                } else {
                    scheduleBackoff();
                }
                break;
            }
            case RAW_SUBSCRIBED: {
                if (raw.packetId < 0) {
                    const uint8_t index =
                        static_cast<uint8_t>(-1 - raw.packetId);
                    emitApplicationEvent(Esp32BaseMqtt::EVENT_SUBSCRIPTION_REJECTED,
                                         Esp32BaseMqtt::ERROR_SUBSCRIBE_FAILED,
                                         0,
                                         index);
                    break;
                }
                uint8_t index = MQTT_NO_SLOT;
                portENTER_CRITICAL(&g_mqttMux);
                for (uint8_t i = 0; i < g_mqttSubscriptionCount; ++i) {
                    if (g_mqttSubscriptions[i].pendingPacketId == raw.packetId) {
                        index = i;
                        g_mqttSubscriptions[i].pendingPacketId = -1;
                        if (!raw.subscriptionRejected) {
                            ++g_mqttDiagnostics.subscriptionAcknowledged;
                        }
                        break;
                    }
                }
                portEXIT_CRITICAL(&g_mqttMux);
                emitApplicationEvent(
                    raw.subscriptionRejected
                        ? Esp32BaseMqtt::EVENT_SUBSCRIPTION_REJECTED
                        : Esp32BaseMqtt::EVENT_SUBSCRIPTION_ACKNOWLEDGED,
                    raw.subscriptionRejected
                        ? Esp32BaseMqtt::ERROR_SUBSCRIBE_FAILED
                        : Esp32BaseMqtt::ERROR_NONE,
                    static_cast<uint16_t>(raw.packetId),
                    index);
                break;
            }
            case RAW_PUBLISHED: {
                bool found = false;
                portENTER_CRITICAL(&g_mqttMux);
                for (uint8_t i = 0; i < ESP32BASE_MQTT_MAX_INFLIGHT_QOS1; ++i) {
                    if (g_mqttInflight[i].used &&
                        g_mqttInflight[i].packetId == raw.packetId) {
                        g_mqttInflight[i].used = false;
                        ++g_mqttDiagnostics.publishAcknowledged;
                        found = true;
                        break;
                    }
                }
                portEXIT_CRITICAL(&g_mqttMux);
                if (found) {
                    emitApplicationEvent(Esp32BaseMqtt::EVENT_PUBLISH_ACKNOWLEDGED,
                                         Esp32BaseMqtt::ERROR_NONE,
                                         static_cast<uint16_t>(raw.packetId));
                }
                break;
            }
        }
    }
}

void dispatchIncomingMessages() {
    for (uint8_t i = 0; i < ESP32BASE_MQTT_INCOMING_SLOTS; ++i) {
        bool ready = false;
        portENTER_CRITICAL(&g_mqttMux);
        if (g_mqttIncoming[i].state == INCOMING_READY) {
            g_mqttIncoming[i].state = INCOMING_DISPATCHING;
            ready = true;
        }
        portEXIT_CRITICAL(&g_mqttMux);
        if (!ready) {
            continue;
        }

        IncomingSlot& slot = g_mqttIncoming[i];
        if (g_mqttMessageCallback) {
            Esp32BaseMqtt::MessageView view;
            view.topic = slot.topic;
            view.topicLength = slot.topicLength;
            view.payload = slot.payload;
            view.payloadLength = slot.payloadLength;
            view.qos = slot.qos;
            view.retain = slot.retain;
            view.duplicate = slot.duplicate;
            g_mqttMessageCallback(view, g_mqttMessageContext);
        }
        portENTER_CRITICAL(&g_mqttMux);
        slot.state = INCOMING_EMPTY;
        portEXIT_CRITICAL(&g_mqttMux);
    }
}

bool startMqttClient() {
    portENTER_CRITICAL(&g_mqttMux);
    ++g_mqttDiagnostics.connectAttempts;
    g_mqttConnectionWanted = true;
    portEXIT_CRITICAL(&g_mqttMux);
    if (g_mqttClient) {
        setState(Esp32BaseMqtt::CONNECTING);
        const esp_err_t result = esp_mqtt_client_reconnect(g_mqttClient);
        if (result != ESP_OK) {
            portENTER_CRITICAL(&g_mqttMux);
            g_mqttDiagnostics.nativeEspError = result;
            portEXIT_CRITICAL(&g_mqttMux);
            setLastError(Esp32BaseMqtt::ERROR_DNS_OR_TRANSPORT);
            scheduleBackoff();
            return false;
        }
        return true;
    }

    esp_mqtt_client_config_t nativeConfig;
    fillNativeConfig(nativeConfig);
    g_mqttClient = esp_mqtt_client_init(&nativeConfig);
    if (!g_mqttClient) {
        setLastError(Esp32BaseMqtt::ERROR_NO_MEMORY);
        portENTER_CRITICAL(&g_mqttMux);
        ++g_mqttDiagnostics.enqueueFailures;
        portEXIT_CRITICAL(&g_mqttMux);
        scheduleBackoff();
        return false;
    }
    if (esp_mqtt_client_register_event(
            g_mqttClient, MQTT_EVENT_ANY, mqttEventHandler, nullptr) != ESP_OK) {
        setLastError(Esp32BaseMqtt::ERROR_NO_MEMORY);
        destroyUnstartedMqttClient();
        scheduleBackoff();
        return false;
    }
    setState(Esp32BaseMqtt::CONNECTING);
    const esp_err_t result = esp_mqtt_client_start(g_mqttClient);
    if (result != ESP_OK) {
        portENTER_CRITICAL(&g_mqttMux);
        g_mqttDiagnostics.nativeEspError = result;
        portEXIT_CRITICAL(&g_mqttMux);
        setLastError(result == ESP_ERR_NO_MEM
                         ? Esp32BaseMqtt::ERROR_NO_MEMORY
                         : Esp32BaseMqtt::ERROR_DNS_OR_TRANSPORT);
        destroyUnstartedMqttClient();
        scheduleBackoff();
        return false;
    }
    return true;
}

uint8_t inflightCountLocked() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < ESP32BASE_MQTT_MAX_INFLIGHT_QOS1; ++i) {
        if (g_mqttInflight[i].used) {
            ++count;
        }
    }
    return count;
}

uint8_t reserveInflightSlot() {
    uint8_t index = MQTT_NO_SLOT;
    portENTER_CRITICAL(&g_mqttMux);
    for (uint8_t i = 0; i < ESP32BASE_MQTT_MAX_INFLIGHT_QOS1; ++i) {
        if (!g_mqttInflight[i].used) {
            g_mqttInflight[i].used = true;
            index = i;
            break;
        }
    }
    portEXIT_CRITICAL(&g_mqttMux);
    return index;
}

void releaseInflightSlot(uint8_t index) {
    if (index >= ESP32BASE_MQTT_MAX_INFLIGHT_QOS1) {
        return;
    }
    portENTER_CRITICAL(&g_mqttMux);
    g_mqttInflight[index].used = false;
    portEXIT_CRITICAL(&g_mqttMux);
}

} // namespace

bool Esp32BaseMqtt::configure(const ConnectionConfig& config) {
    g_mqttConfigurationAttempted = true;
    if (g_mqttBegun ||
        !validRequiredString(config.host, MQTT_MAX_HOST_BYTES) ||
        !validRequiredString(config.clientId, MQTT_MAX_CLIENT_ID_BYTES) ||
        config.port == 0 ||
        config.keepAliveSeconds < MQTT_MIN_KEEPALIVE_SECONDS ||
        config.keepAliveSeconds > MQTT_MAX_KEEPALIVE_SECONDS ||
        !validOptionalString(config.username, MQTT_MAX_USERNAME_BYTES) ||
        !validOptionalString(config.password, MQTT_MAX_PASSWORD_BYTES) ||
        (config.password && !config.username) ||
        (config.security != TLS && config.security != EXPLICIT_PLAINTEXT)) {
        g_mqttConfigured = false;
        setState(CONFIGURATION_ERROR);
        setLastError(ERROR_INVALID_CONFIGURATION);
        return false;
    }

    if (config.security == TLS) {
        if (!validCertificatePem(config.tls.caCertificatePem,
                                 config.tls.caCertificateLength,
                                 ESP32BASE_MQTT_MAX_CA_CERT_BYTES)) {
            g_mqttConfigured = false;
            setState(CONFIGURATION_ERROR);
            setLastError(ERROR_INVALID_CONFIGURATION);
            return false;
        }
        const bool clientCertificateSet =
            config.tls.clientCertificatePem || config.tls.clientCertificateLength != 0;
        const bool privateKeySet =
            config.tls.privateKeyPem || config.tls.privateKeyLength != 0;
        if (clientCertificateSet != privateKeySet ||
            (clientCertificateSet &&
             (!validCertificatePem(config.tls.clientCertificatePem,
                                   config.tls.clientCertificateLength,
                                   ESP32BASE_MQTT_MAX_IDENTITY_PEM_BYTES) ||
              !validPem(config.tls.privateKeyPem,
                        config.tls.privateKeyLength,
                        ESP32BASE_MQTT_MAX_IDENTITY_PEM_BYTES)))) {
            g_mqttConfigured = false;
            setState(CONFIGURATION_ERROR);
            setLastError(ERROR_INVALID_CONFIGURATION);
            return false;
        }
    } else {
#if !ESP32BASE_MQTT_ALLOW_PLAINTEXT
        g_mqttConfigured = false;
        setState(CONFIGURATION_ERROR);
        setLastError(ERROR_INVALID_CONFIGURATION);
        return false;
#endif
        if (config.tls.caCertificatePem || config.tls.clientCertificatePem ||
            config.tls.privateKeyPem) {
            g_mqttConfigured = false;
            setState(CONFIGURATION_ERROR);
            setLastError(ERROR_INVALID_CONFIGURATION);
            return false;
        }
    }

    if (config.lastWill) {
        if (!validPublishTopic(config.lastWill->topic) ||
            config.lastWill->payloadLength > ESP32BASE_MQTT_MAX_PAYLOAD_BYTES ||
            (config.lastWill->payloadLength > 0 && !config.lastWill->payload) ||
            (config.lastWill->qos != QOS_0 && config.lastWill->qos != QOS_1)) {
            g_mqttConfigured = false;
            setState(CONFIGURATION_ERROR);
            setLastError(ERROR_INVALID_CONFIGURATION);
            return false;
        }
    }

    g_mqttConfig = config;
    g_mqttConfigured = true;
    setState(WAITING_FOR_WIFI);
    setLastError(ERROR_NONE);
    return true;
}

bool Esp32BaseMqtt::addSubscription(const Subscription& subscription) {
    if (g_mqttBegun || !validTopicFilter(subscription.topicFilter) ||
        (subscription.qos != QOS_0 && subscription.qos != QOS_1)) {
        setLastError(ERROR_INVALID_CONFIGURATION);
        return false;
    }
    if (g_mqttSubscriptionCount >= ESP32BASE_MQTT_MAX_SUBSCRIPTIONS) {
        setLastError(ERROR_SUBSCRIPTION_LIMIT);
        return false;
    }
    for (uint8_t i = 0; i < g_mqttSubscriptionCount; ++i) {
        if (strcmp(g_mqttSubscriptions[i].topicFilter,
                   subscription.topicFilter) == 0) {
            setLastError(ERROR_INVALID_CONFIGURATION);
            return false;
        }
    }
    SubscriptionEntry& entry = g_mqttSubscriptions[g_mqttSubscriptionCount++];
    entry.topicFilter = subscription.topicFilter;
    entry.qos = subscription.qos;
    entry.pendingPacketId = -1;
    return true;
}

void Esp32BaseMqtt::setMessageCallback(MessageCallback callback, void* context) {
    g_mqttMessageCallback = callback;
    g_mqttMessageContext = context;
}

void Esp32BaseMqtt::setEventCallback(EventCallback callback, void* context) {
    g_mqttEventCallback = callback;
    g_mqttEventContext = context;
}

Esp32BaseMqtt::PublishResult Esp32BaseMqtt::publish(const PublishRequest& request) {
    PublishResult result;
    if (!g_mqttConfigured) {
        result.code = PUBLISH_NOT_CONFIGURED;
        return result;
    }
    if (g_mqttState != CONNECTED || !g_mqttClient) {
        result.code = PUBLISH_NOT_CONNECTED;
        setLastError(ERROR_NOT_CONNECTED);
        return result;
    }
    if (!request.topic || request.topic[0] == '\0' ||
        strchr(request.topic, '#') || strchr(request.topic, '+')) {
        result.code = PUBLISH_INVALID_TOPIC;
        setLastError(ERROR_INVALID_CONFIGURATION);
        return result;
    }
    if (boundedLength(request.topic, ESP32BASE_MQTT_MAX_TOPIC_BYTES) >
        ESP32BASE_MQTT_MAX_TOPIC_BYTES) {
        result.code = PUBLISH_TOPIC_TOO_LONG;
        setLastError(ERROR_TOPIC_TOO_LONG);
        return result;
    }
    if (request.payloadLength > 0 && !request.payload) {
        result.code = PUBLISH_INVALID_PAYLOAD;
        setLastError(ERROR_INVALID_CONFIGURATION);
        return result;
    }
    if (request.payloadLength > ESP32BASE_MQTT_MAX_PAYLOAD_BYTES) {
        result.code = PUBLISH_PAYLOAD_TOO_LONG;
        setLastError(ERROR_PAYLOAD_TOO_LONG);
        return result;
    }
    if (request.qos != QOS_0 && request.qos != QOS_1) {
        result.code = PUBLISH_INVALID_QOS;
        setLastError(ERROR_INVALID_CONFIGURATION);
        return result;
    }

    const int currentOutbox = esp_mqtt_client_get_outbox_size(g_mqttClient);
    const size_t estimatedPacketBytes =
        strlen(request.topic) + request.payloadLength + 32;
    if (currentOutbox < 0 ||
        static_cast<size_t>(currentOutbox) + estimatedPacketBytes >
            ESP32BASE_MQTT_MAX_OUTBOX_BYTES) {
        result.code = PUBLISH_OUTBOX_FULL;
        setLastError(ERROR_OUTBOX_FULL);
        return result;
    }

    uint8_t inflightSlot = MQTT_NO_SLOT;
    if (request.qos == QOS_1) {
        inflightSlot = reserveInflightSlot();
        if (inflightSlot == MQTT_NO_SLOT) {
            result.code = PUBLISH_INFLIGHT_LIMIT;
            setLastError(ERROR_INFLIGHT_LIMIT);
            return result;
        }
    }

    const int packetId = esp_mqtt_client_enqueue(
        g_mqttClient,
        request.topic,
        reinterpret_cast<const char*>(request.payload),
        static_cast<int>(request.payloadLength),
        static_cast<int>(request.qos),
        request.retain ? 1 : 0,
        true);
    if (packetId < 0) {
        releaseInflightSlot(inflightSlot);
        portENTER_CRITICAL(&g_mqttMux);
        ++g_mqttDiagnostics.enqueueFailures;
        portEXIT_CRITICAL(&g_mqttMux);
        result.code = packetId == -2 ? PUBLISH_OUTBOX_FULL : PUBLISH_NO_MEMORY;
        setLastError(packetId == -2 ? ERROR_OUTBOX_FULL : ERROR_NO_MEMORY);
        return result;
    }

    if (request.qos == QOS_1) {
        portENTER_CRITICAL(&g_mqttMux);
        g_mqttInflight[inflightSlot].packetId = static_cast<uint16_t>(packetId);
        portEXIT_CRITICAL(&g_mqttMux);
    }
    const int outboxAfter = esp_mqtt_client_get_outbox_size(g_mqttClient);
    portENTER_CRITICAL(&g_mqttMux);
    ++g_mqttDiagnostics.publishAccepted;
    if (outboxAfter > 0 &&
        outboxAfter > g_mqttDiagnostics.outboxHighWaterBytes) {
        g_mqttDiagnostics.outboxHighWaterBytes =
            static_cast<uint16_t>(outboxAfter > UINT16_MAX ? UINT16_MAX : outboxAfter);
    }
    portEXIT_CRITICAL(&g_mqttMux);
    result.code = PUBLISH_ACCEPTED;
    result.packetId = request.qos == QOS_1
                          ? static_cast<uint16_t>(packetId)
                          : 0;
    return result;
}

bool Esp32BaseMqtt::requestReconnect() {
    if (!g_mqttBegun || !g_mqttConfigured) {
        return false;
    }
    portENTER_CRITICAL(&g_mqttMux);
    g_mqttReconnectRequested = true;
    portEXIT_CRITICAL(&g_mqttMux);
    return true;
}

bool Esp32BaseMqtt::begin() {
    g_mqttBegun = true;
    if (g_mqttConfigurationAttempted && !g_mqttConfigured) {
        return false;
    }
    if (!g_mqttConfigured) {
        setState(NOT_CONFIGURED);
        ESP32BASE_LOG_I("mqtt", "not_configured enabled=true");
        return true;
    }
    portENTER_CRITICAL(&g_mqttMux);
    g_mqttConnectionWanted = true;
    portEXIT_CRITICAL(&g_mqttMux);
    setState(WAITING_FOR_WIFI);
    ESP32BASE_LOG_I("mqtt",
                    "configured host=%s port=%u client_id=%s tls=%s credential_set=%s client_certificate_set=%s clean_session=true",
                    g_mqttConfig.host,
                    static_cast<unsigned>(g_mqttConfig.port),
                    g_mqttConfig.clientId,
                    g_mqttConfig.security == TLS ? "true" : "false",
                    g_mqttConfig.username ? "true" : "false",
                    g_mqttConfig.tls.clientCertificatePem ? "true" : "false");
    return true;
}

void Esp32BaseMqtt::handle(bool otaUploading) {
    if (g_mqttConfigured && otaUploading && !g_mqttOtaSuspended) {
        g_mqttOtaSuspended = true;
        g_mqttForceReconnectAfterDisconnect = false;
        requestNativeDisconnect();
        setState(SUSPENDED_FOR_OTA);
        ESP32BASE_LOG_I("mqtt", "suspended reason=ota_upload");
    }

    handleRawEvents();
    dispatchIncomingMessages();
    dispatchMailboxOverflowIfNeeded();
    dispatchIncomingDropIfNeeded();

    if (!g_mqttConfigured) {
        return;
    }
    bool reconnectRequested = false;
    portENTER_CRITICAL(&g_mqttMux);
    reconnectRequested = g_mqttReconnectRequested;
    g_mqttReconnectRequested = false;
    portEXIT_CRITICAL(&g_mqttMux);
    if (otaUploading) {
        return;
    }
    if (g_mqttOtaSuspended) {
        g_mqttOtaSuspended = false;
        portENTER_CRITICAL(&g_mqttMux);
        g_mqttConnectionWanted = true;
        portEXIT_CRITICAL(&g_mqttMux);
        g_mqttNextAttemptMs = millis();
        g_mqttBackoffStep = 0;
    }
    if (reconnectRequested) {
        g_mqttTerminalRejection = false;
        setLastError(ERROR_NONE);
        g_mqttBackoffStep = 0;
        g_mqttNextAttemptMs = millis();
        if (g_mqttState == CONNECTED || g_mqttState == CONNECTING) {
            g_mqttForceReconnectAfterDisconnect = true;
            requestNativeDisconnect();
        } else {
            portENTER_CRITICAL(&g_mqttMux);
            g_mqttConnectionWanted = true;
            portEXIT_CRITICAL(&g_mqttMux);
            setState(BACKOFF);
        }
        return;
    }
    if (!Esp32BaseWiFi::isConnected()) {
        if (isConnectionWanted()) {
            requestNativeDisconnect();
        }
        setState(WAITING_FOR_WIFI);
        return;
    }
    if (g_mqttConfig.security == TLS && !Esp32BaseTime::isRealTime()) {
        if (isConnectionWanted()) {
            requestNativeDisconnect();
        }
        setState(WAITING_FOR_TIME);
        return;
    }
    if (!isConnectionWanted()) {
        portENTER_CRITICAL(&g_mqttMux);
        g_mqttConnectionWanted = true;
        portEXIT_CRITICAL(&g_mqttMux);
        g_mqttBackoffStep = 0;
        g_mqttNextAttemptMs = millis();
        setState(BACKOFF);
    }
    if (g_mqttState == CONNECTION_REJECTED || g_mqttState == CONNECTED ||
        g_mqttState == CONNECTING) {
        return;
    }
    const uint32_t now = millis();
    if (!deadlineReached(now, g_mqttNextAttemptMs)) {
        setState(BACKOFF);
        return;
    }
    startMqttClient();
}

void Esp32BaseMqtt::prepareForLifecycleStop() {
    if (!g_mqttBegun) {
        return;
    }
    setState(STOPPING);
    clearInflight(false);
    requestNativeDisconnect();
}

Esp32BaseMqtt::State Esp32BaseMqtt::state() {
    portENTER_CRITICAL(&g_mqttMux);
    const State current = g_mqttState;
    portEXIT_CRITICAL(&g_mqttMux);
    return current;
}

Esp32BaseMqtt::Status Esp32BaseMqtt::status() {
    Status snapshot;
    portENTER_CRITICAL(&g_mqttMux);
    snapshot.state = g_mqttState;
    snapshot.lastError = g_mqttLastError;
    snapshot.configured = g_mqttConfigured;
    snapshot.connectedAtUptimeMs = g_mqttConnectedAtUptimeMs;
    snapshot.connectedAtEpochSec = g_mqttConnectedAtEpochSec;
    portEXIT_CRITICAL(&g_mqttMux);
    snapshot.tls = g_mqttConfig.security == TLS;
    snapshot.usernameSet = g_mqttConfig.username != nullptr;
    snapshot.clientCertificateSet =
        g_mqttConfig.tls.clientCertificatePem != nullptr;
    snapshot.host = g_mqttConfigured ? g_mqttConfig.host : nullptr;
    snapshot.port = g_mqttConfigured ? g_mqttConfig.port : 0;
    snapshot.clientId = g_mqttConfigured ? g_mqttConfig.clientId : nullptr;
    return snapshot;
}

Esp32BaseMqtt::Diagnostics Esp32BaseMqtt::diagnostics() {
    portENTER_CRITICAL(&g_mqttMux);
    Diagnostics snapshot = g_mqttDiagnostics;
    snapshot.inflightQos1 = inflightCountLocked();
    portEXIT_CRITICAL(&g_mqttMux);
    return snapshot;
}

const char* Esp32BaseMqtt::stateName(State stateValue) {
    switch (stateValue) {
        case NOT_CONFIGURED: return "not_configured";
        case CONFIGURATION_ERROR: return "configuration_error";
        case WAITING_FOR_WIFI: return "waiting_for_wifi";
        case WAITING_FOR_TIME: return "waiting_for_time";
        case BACKOFF: return "backoff";
        case CONNECTING: return "connecting";
        case CONNECTED: return "connected";
        case CONNECTION_REJECTED: return "connection_rejected";
        case SUSPENDED_FOR_OTA: return "suspended_for_ota";
        case STOPPING: return "stopping";
        default: return "unknown";
    }
}

const char* Esp32BaseMqtt::errorName(Error errorValue) {
    switch (errorValue) {
        case ERROR_NONE: return "none";
        case ERROR_INVALID_CONFIGURATION: return "invalid_configuration";
        case ERROR_NOT_CONNECTED: return "not_connected";
        case ERROR_TOPIC_TOO_LONG: return "topic_too_long";
        case ERROR_PAYLOAD_TOO_LONG: return "payload_too_long";
        case ERROR_SUBSCRIPTION_LIMIT: return "subscription_limit";
        case ERROR_INFLIGHT_LIMIT: return "inflight_limit";
        case ERROR_OUTBOX_FULL: return "outbox_full";
        case ERROR_NO_MEMORY: return "no_memory";
        case ERROR_SUBSCRIBE_FAILED: return "subscribe_failed";
        case ERROR_DNS_OR_TRANSPORT: return "dns_or_transport";
        case ERROR_TLS: return "tls";
        case ERROR_TLS_CERTIFICATE: return "tls_certificate";
        case ERROR_BROKER_UNAVAILABLE: return "broker_unavailable";
        case ERROR_PROTOCOL_REJECTED: return "protocol_rejected";
        case ERROR_CLIENT_ID_REJECTED: return "client_id_rejected";
        case ERROR_BAD_CREDENTIALS: return "bad_credentials";
        case ERROR_NOT_AUTHORIZED: return "not_authorized";
        case ERROR_DISCONNECTED: return "disconnected";
        default: return "unknown";
    }
}
