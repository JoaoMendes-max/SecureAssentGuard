#include "C_RDM6300.h"
#include <iostream>
#include <cstring>
#include <unistd.h> // usleep

C_RDM6300::C_RDM6300(C_UART& uart)
    : C_Sensor(ID_RDM6300), m_uart(uart)
{
}

C_RDM6300::~C_RDM6300() = default;

bool C_RDM6300::init() {
    if (!m_uart.openPort()) return false;
    if (!m_uart.configPort(9600, 8, 'N')) return false;
    return true;
}

bool C_RDM6300::read(SensorData* data) {
    char header = 0;    // Read the first byte, must be STX (0x02)
    if (m_uart.readBuffer(&header, 1) != 1) return false;

    if (header != RDM_STX) {
        // Clear buffer on invalid header
        char trash[32];
        while(m_uart.readBuffer(trash, 32) > 0);
        return false;
    }
    m_rawBuffer[0] = RDM_STX;
    int bytesToRead = 13;
    int totalRead = 0;
    int attempts = 0;
    while (totalRead < bytesToRead && attempts < 10) {
        int n = m_uart.readBuffer(m_rawBuffer + 1 + totalRead, bytesToRead - totalRead);
        if (n > 0) {
            totalRead += n;
        } else {
            usleep(10000); // 10ms
            attempts++;
        }
    }

    if (totalRead < bytesToRead) {return true;}

    bool success = false;
    if (m_rawBuffer[13] == RDM_ETX) {
        if (validateChecksum(m_rawBuffer)) {
            success = true;
            if (data) {
                data->type = ID_RDM6300;
                parseTag(m_rawBuffer, data->data.rfid_single.tagID);
            }
        }
    }

    char trash[64];
    while(m_uart.readBuffer(trash, 64) > 0); // para limpar no fim
    return success;
}

bool C_RDM6300::validateChecksum(const char* buffer) {
    uint8_t calcChecksum = 0;

    for (int i = 0; i < 5; i++) {
        uint8_t val = hexPairToByte(buffer[1 + (i * 2)], buffer[2 + (i * 2)]);
        calcChecksum ^= val;
    }

    uint8_t receivedChecksum = hexPairToByte(buffer[11], buffer[12]);

    return (calcChecksum == receivedChecksum);
}

void C_RDM6300::parseTag(const char* buffer, char* dest) {
    memcpy(dest, buffer + 1, 10);
    dest[10] = '\0';
}

uint8_t C_RDM6300::asciiCharToVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

uint8_t C_RDM6300::hexPairToByte(char high, char low) {
    return (asciiCharToVal(high) << 4) | asciiCharToVal(low);
}
