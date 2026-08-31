#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t esp_err_t;
static constexpr esp_err_t ESP_OK = 0;
static constexpr esp_err_t ESP_FAIL = -1;
static constexpr esp_err_t ESP_ERR_NO_MEM = 0x101;

typedef const char* esp_event_base_t;
typedef void (*esp_event_handler_t)(void*, esp_event_base_t, int32_t, void*);

struct esp_mqtt_client;
typedef esp_mqtt_client* esp_mqtt_client_handle_t;

enum esp_mqtt_event_id_t {
    MQTT_EVENT_ANY = -1,
    MQTT_EVENT_ERROR = 0,
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_SUBSCRIBED,
    MQTT_EVENT_UNSUBSCRIBED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_DATA,
    MQTT_EVENT_BEFORE_CONNECT
};

enum esp_mqtt_connect_return_code_t {
    MQTT_CONNECTION_ACCEPTED = 0,
    MQTT_CONNECTION_REFUSE_PROTOCOL,
    MQTT_CONNECTION_REFUSE_ID_REJECTED,
    MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE,
    MQTT_CONNECTION_REFUSE_BAD_USERNAME,
    MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED
};

enum esp_mqtt_error_type_t {
    MQTT_ERROR_TYPE_NONE = 0,
    MQTT_ERROR_TYPE_TCP_TRANSPORT,
    MQTT_ERROR_TYPE_CONNECTION_REFUSED
};

enum esp_mqtt_transport_t {
    MQTT_TRANSPORT_UNKNOWN = 0,
    MQTT_TRANSPORT_OVER_TCP,
    MQTT_TRANSPORT_OVER_SSL,
    MQTT_TRANSPORT_OVER_WS,
    MQTT_TRANSPORT_OVER_WSS
};

enum esp_mqtt_protocol_ver_t {
    MQTT_PROTOCOL_UNDEFINED = 0,
    MQTT_PROTOCOL_V_3_1,
    MQTT_PROTOCOL_V_3_1_1
};

struct esp_mqtt_error_codes_t {
    esp_err_t esp_tls_last_esp_err;
    int esp_tls_stack_err;
    int esp_tls_cert_verify_flags;
    esp_mqtt_error_type_t error_type;
    esp_mqtt_connect_return_code_t connect_return_code;
    int esp_transport_sock_errno;
};

struct esp_mqtt_event_t {
    esp_mqtt_event_id_t event_id;
    esp_mqtt_client_handle_t client;
    void* user_context;
    char* data;
    int data_len;
    int total_data_len;
    int current_data_offset;
    char* topic;
    int topic_len;
    int msg_id;
    int session_present;
    esp_mqtt_error_codes_t* error_handle;
    bool retain;
    int qos;
    bool dup;
};

struct esp_mqtt_client_config_t {
    void* event_handle;
    void* event_loop_handle;
    const char* host;
    const char* uri;
    uint32_t port;
    bool set_null_client_id;
    const char* client_id;
    const char* username;
    const char* password;
    const char* lwt_topic;
    const char* lwt_msg;
    int lwt_qos;
    int lwt_retain;
    int lwt_msg_len;
    int disable_clean_session;
    int keepalive;
    bool disable_auto_reconnect;
    void* user_context;
    int task_prio;
    int task_stack;
    int buffer_size;
    const char* cert_pem;
    size_t cert_len;
    const char* client_cert_pem;
    size_t client_cert_len;
    const char* client_key_pem;
    size_t client_key_len;
    esp_mqtt_transport_t transport;
    int refresh_connection_after_ms;
    const void* psk_hint_key;
    bool use_global_ca_store;
    void* crt_bundle_attach;
    int reconnect_timeout_ms;
    const char** alpn_protos;
    const char* clientkey_password;
    int clientkey_password_len;
    esp_mqtt_protocol_ver_t protocol_ver;
    int out_buffer_size;
    bool skip_cert_common_name_check;
    bool use_secure_element;
    void* ds_data;
    int network_timeout_ms;
};

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t*);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_disconnect(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_reconnect(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t);
esp_err_t esp_mqtt_set_config(
    esp_mqtt_client_handle_t, const esp_mqtt_client_config_t*);
esp_err_t esp_mqtt_client_register_event(
    esp_mqtt_client_handle_t, esp_mqtt_event_id_t, esp_event_handler_t, void*);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char*, int);
int esp_mqtt_client_enqueue(
    esp_mqtt_client_handle_t, const char*, const char*, int, int, int, bool);
int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t);
