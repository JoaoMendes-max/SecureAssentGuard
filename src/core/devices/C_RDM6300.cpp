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
    if (!m_uart.openPort()) return false;
    // RDM6300 default: 9600 baud, 8 data bits, no parity, 1 stop bit
    if (!m_uart.configPort(9600, 8, 'N')) return false;
    return true;
}

// Wait for data with timeout using poll()
// Returns true if data available, false on timeout
bool C_RDM6300::waitForData(int timeout_ms) const {
    struct pollfd pfd;
    pfd.fd = m_uart.getFd();
    pfd.events = POLLIN;     // Wait for read-ready

    // poll() blocks without CPU usage
    int ret = poll(&pfd, 1, timeout_ms);

    return (ret > 0);        // >0 = data ready, 0 = timeout, <0 = error
}

bool C_RDM6300::read(SensorData* data) {
    /* Wait for packet start (STX byte) with 1s timeout.
       If no card present, returns quickly allowing thread exit checks. */
    if (!waitForData(1000)) {
        cerr << "shit falhou" << endl;
        return false;
    }
    // Read STX byte (0x02)
    char header = 0;
    if (m_uart.readBuffer(&header, 1) != 1) return false;
    /* Validate STX - if not 0x02, we have garbage data.
       Clean buffer to avoid synchronization issues. */
    if (header != RDM_STX) {
        char trash[32];
        if (waitForData(10)) {    // Brief wait for remaining garbage
            while(m_uart.readBuffer(trash, sizeof(trash)) > 0);
        }
        return false;
    }
    // Start building frame: STX at position 0
    m_rawBuffer[0] = RDM_STX;
    int bytesToRead = 13;    // Remaining: 10 data + 2 checksum + 1 ETX
    int totalRead = 0;
    /* Read frame body with 100ms timeout.
       At 9600 bps (~1ms/byte), 100ms is ample for 13 bytes. */
    while (totalRead < bytesToRead) {
        if (!waitForData(100)) {
            return false;    // Frame incomplete/corrupted
        }

        int n = m_uart.readBuffer(m_rawBuffer + 1 + totalRead,
                                 bytesToRead - totalRead);
        if (n > 0) {
            totalRead += n;
        } else {
            return false;    // Read error
        }
    }
    // Validate frame structure and checksum
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
    /* Non-blocking buffer cleanup.
       Remove any leftover bytes to keep UART sync. */
    if (waitForData(0)) {
        char trash[64];
        while(m_uart.readBuffer(trash, sizeof(trash)) > 0);
    }
    return success;
}

/*
* Helper functions
*/

/* RDM6300 checksum: XOR of 5 data bytes.
   Frame format: STX(1) + DATA(10) + CHECKSUM(2) + ETX(1) */
bool C_RDM6300::validateChecksum(const char* buffer) {
    uint8_t calcChecksum = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t val = hexPairToByte(buffer[1 + (i * 2)], buffer[2 + (i * 2)]);
        calcChecksum ^= val;
    }
    uint8_t receivedChecksum = hexPairToByte(buffer[11], buffer[12]);
    return (calcChecksum == receivedChecksum);
}

// Extract 10-character ASCII tag from frame (bytes 1-10)
void C_RDM6300::parseTag(const char* buffer, char* dest) {
    memcpy(dest, buffer + 1, 10);
    dest[10] = '\0';
}

// Convert ASCII hex char to 4-bit value
uint8_t C_RDM6300::asciiCharToVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;  // Invalid char
}

// Convert two ASCII hex chars to one byte
uint8_t C_RDM6300::hexPairToByte(char high, char low) {
    return (asciiCharToVal(high) << 4) | asciiCharToVal(low);
}