/*
 * RDM6300 RFID reader implementation.
 */

#include "C_RDM6300.h"
#include "C_UART.h"
#include <iostream>
#include <cstring>
#include <poll.h>
#include <unistd.h>

C_RDM6300::C_RDM6300(C_UART& uart)
    : C_Sensor(ID_RDM6300), m_uart(uart) {}

C_RDM6300::~C_RDM6300() = default;

bool C_RDM6300::init() {
    // Open UART and configure 9600 8N1.
    if (!m_uart.openPort()) return false;
    
    if (!m_uart.configPort(9600, 8, 'N')) return false;
    return true;
}
bool C_RDM6300::waitForData(int timeout_ms) const {
    // Poll UART for incoming data.
    struct pollfd pfd;
    pfd.fd = m_uart.getFd();
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, timeout_ms);

    return (ret > 0);        
}

bool C_RDM6300::read(SensorData* data) {
    if (!waitForData(1000)) {
        cerr << "shit falhou" << endl;
        return false;
    }
    char header = 0;
    if (m_uart.readBuffer(&header, 1) != 1) return false;
    
    // Expect start-of-text marker; otherwise flush noise.
    if (header != RDM_STX) {
        char trash[32];
        if (waitForData(10)) {    
            while(m_uart.readBuffer(trash, sizeof(trash)) > 0);
        }
        return false;
    }
    m_rawBuffer[0] = RDM_STX;
    int bytesToRead = 13;    
    int totalRead = 0;
    while (totalRead < bytesToRead) {
        if (!waitForData(100)) {
            return false;    
        }

        int n = m_uart.readBuffer(m_rawBuffer + 1 + totalRead,
                                 bytesToRead - totalRead);
        if (n > 0) {
            totalRead += n;
        } else {
            return false;    
        }
    }
    
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
    
    // Flush any trailing bytes in the buffer.
    if (waitForData(0)) {
        char trash[64];
        while(m_uart.readBuffer(trash, sizeof(trash)) > 0);
    }
    return success;
}




bool C_RDM6300::validateChecksum(const char* buffer) {
    // XOR of 5 bytes must match checksum field.
    uint8_t calcChecksum = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t val = hexPairToByte(buffer[1 + (i * 2)], buffer[2 + (i * 2)]);
        calcChecksum ^= val;
    }
    uint8_t receivedChecksum = hexPairToByte(buffer[11], buffer[12]);
    return (calcChecksum == receivedChecksum);
}


void C_RDM6300::parseTag(const char* buffer, char* dest) {
    // Copy 10 ASCII hex chars.
    memcpy(dest, buffer + 1, 10);
    dest[10] = '\0';
}


uint8_t C_RDM6300::asciiCharToVal(char c) {
    // Convert ASCII hex to nibble value.
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;  
}


uint8_t C_RDM6300::hexPairToByte(char high, char low) {
    // Pack two ASCII hex chars into a byte.
    return (asciiCharToVal(high) << 4) | asciiCharToVal(low);
}
