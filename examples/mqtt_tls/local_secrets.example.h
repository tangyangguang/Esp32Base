#pragma once

// Copy this file to local_secrets.h and replace every placeholder locally.
// local_secrets.h is ignored by Git.
namespace mqtt_tls_example {

static const char BROKER_HOST[] = "mqtt.example.invalid";
static constexpr uint16_t BROKER_PORT = 8883;
static const char CLIENT_ID[] = "esp32base-mqtt-example";
static const char USERNAME[] = "";
static const char PASSWORD[] = "";

// The example value is deliberately not a real trust anchor. Replace it with
// the CA that validates the configured broker.
static const char CA_CERTIFICATE[] =
    "-----BEGIN CERTIFICATE-----\n"
    "REPLACE WITH THE BROKER CA CERTIFICATE\n"
    "-----END CERTIFICATE-----\n";

} // namespace mqtt_tls_example
