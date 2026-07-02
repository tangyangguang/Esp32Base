#include <Arduino.h>
#include <Esp32Base.h>

namespace {
constexpr int8_t RS485_RX_PIN = 16;
constexpr int8_t RS485_TX_PIN = 17;
constexpr int8_t RS485_DE_PIN = 4;
constexpr uint32_t RS485_BAUD = 9600;
constexpr uint32_t RS485_TURNAROUND_DELAY_US = 50;

Esp32BaseRs485Port g_rs485(Serial2);
uint32_t g_lastTxMs = 0;

void printReceivedByte(int value) {
    Serial.print("rs485 rx 0x");
    if (value < 0x10) {
        Serial.print('0');
    }
    Serial.print(value, HEX);
    if (value >= 32 && value <= 126) {
        Serial.print(" '");
        Serial.write(static_cast<uint8_t>(value));
        Serial.print("'");
    }
    Serial.println();
}
}

void setup() {
    Serial.begin(115200);

    Esp32Base::setFirmwareInfo("rs485_port", "1.0.0");
    Esp32Base::begin();

    if (!g_rs485.configure(RS485_RX_PIN,
                           RS485_TX_PIN,
                           RS485_DE_PIN,
                           RS485_BAUD,
                           SERIAL_8N1,
                           RS485_TURNAROUND_DELAY_US) ||
        !g_rs485.begin()) {
        Serial.println("rs485 begin failed");
        return;
    }
    Serial.println("rs485 ready");
}

void loop() {
    Esp32Base::handle();

    while (g_rs485.readable()) {
        const int value = g_rs485.readByte();
        if (value >= 0) {
            printReceivedByte(value);
        }
    }

    const uint32_t now = millis();
    if (now - g_lastTxMs >= 5000) {
        g_lastTxMs = now;
        g_rs485.writeBytes("ping\r\n");
        Serial.println("rs485 tx ping");
    }

    delay(10);
}
