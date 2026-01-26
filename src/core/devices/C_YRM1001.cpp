/*
 * YRM1001 RFID reader implementation (inventory scan).
 */

#include "C_YRM1001.h"
#include "C_UART.h"
#include "C_GPIO.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <chrono>


static const uint8_t CMD_START_INVENTORY[] = {
    0xBB, 0x00, 0x27, 0x00, 0x03, 0x22, 0xFF, 0xFF, 0x4A, 0x7E
};

static const uint8_t CMD_STOP_INVENTORY[] = {
    0xBB, 0x00, 0x28, 0x00, 0x00, 0x28, 0x7E
};


C_YRM1001::C_YRM1001(C_UART& uart, C_GPIO& enable)
    : C_Sensor(ID_YRM1001),
      m_uart(uart),
      m_gpio_enable(enable),
      m_bufferPos(0)
{
    // Clear RX buffer state.
    std::memset(m_rawBuffer, 0, sizeof(m_rawBuffer));
}

C_YRM1001::~C_YRM1001() {
    powerOff();
}


bool C_YRM1001::init() {
    // Initialize GPIO and UART.
    if (!m_gpio_enable.init()) {
        std::cerr << "[YRM1001] ERROR: Failed to initialize GPIO Enable" << std::endl;
        return false;
    }

    if (!m_uart.openPort()) {
        std::cerr << "[YRM1001] ERROR: Failed to open UART" << std::endl;
        return false;
    }
    
    if (!m_uart.configPort(115200, 8, 'N')) {
        std::cerr << "[YRM1001] ERROR: Failed to configure UART (115200 8N1)" << std::endl;
        return false;
    }

    // Ensure module starts powered off.
    powerOff();

    std::cout << "[YRM1001] Initialized (UART 115200, EN ready)" << std::endl;
    return true;
}




bool C_YRM1001::powerOn() {
    // Enable module power.
    m_gpio_enable.writePin(true);  
    return true;
}


void C_YRM1001::powerOff() {
    // Disable module power.
    m_gpio_enable.writePin(false);  
    std::cout << "[YRM1001] Power OFF" << std::endl;
}


void C_YRM1001::flushUART() const {
    // Drain UART RX to avoid stale frames.
    uint8_t trash[64];
    int count = 0;
    
    while (m_uart.readBuffer(trash, sizeof(trash)) > 0 && ++count < 10);

    if (count > 0) {
        std::cout << "[YRM1001] Flush: Cleared UART buffer" << std::endl;
    }
}


bool C_YRM1001::sendCommand(const uint8_t* cmd, size_t len) const {
    // Send raw command frame.
    int written = m_uart.writeBuffer(cmd, len);

    if (written != static_cast<int>(len)) {
        std::cerr << "[YRM1001] ERROR: Failed to send command" << std::endl;
        return false;
    }

    return true;
}


bool C_YRM1001::readFrame() {
    m_bufferPos = 0;

    struct pollfd pfd;
    pfd.fd = m_uart.getFd();
    pfd.events = POLLIN;

    const int HEADER_SIZE = 5;

    while (m_bufferPos < HEADER_SIZE) {
        
        int ret = poll(&pfd, 1, 100);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            int n = m_uart.readBuffer(&m_rawBuffer[m_bufferPos], HEADER_SIZE - m_bufferPos);

            if (n > 0) {
                m_bufferPos += n;
            }
        } else if (ret == 0) {
            // Timeout waiting for header.
            return false;
        } else {
            // Poll error on header.
            std::cerr << "[YRM1001] ERROR: Poll failed on header" << std::endl;
            return false;
        }
    }

    // Validate frame header byte.
    if (m_rawBuffer[YRM_IDX_HEADER] != YRM_HEADER) {
        std::cerr << "[YRM1001] ERROR: Invalid header (0x"
                  << std::hex << static_cast<int>(m_rawBuffer[0]) << std::dec << ")" << std::endl;
        return false;
    }
    
    uint16_t payloadLen = (m_rawBuffer[YRM_IDX_PL_MSB] << 8) | m_rawBuffer[YRM_IDX_PL_LSB];
    int totalFrameSize = 5 + payloadLen + 2;

    if (totalFrameSize > static_cast<int>(sizeof(m_rawBuffer))) {
        std::cerr << "[YRM1001] ERROR: Frame too large (" << totalFrameSize << " bytes)" << std::endl;
        return false;
    }

    int remaining = totalFrameSize - HEADER_SIZE;
    while (m_bufferPos < totalFrameSize) {
        // Read the remaining payload + checksum + tail.
        int ret = poll(&pfd, 1, 100);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            // Read available bytes.
            int n = m_uart.readBuffer(&m_rawBuffer[m_bufferPos], remaining);

            if (n > 0) {
                m_bufferPos += n;
                remaining -= n;
            }
        } else if (ret == 0) {
            // Timeout mid-frame.
            std::cerr << "[YRM1001] ERROR: Timeout mid-frame (got "
                      << m_bufferPos << "/" << totalFrameSize << " bytes)" << std::endl;
            return false;
        } else {
            // Poll error on payload.
            std::cerr << "[YRM1001] ERROR: Poll failed on payload" << std::endl;
            return false;
        }
    }

    // Validate tail byte.
    if (m_rawBuffer[m_bufferPos - 1] != YRM_TAIL) {
        std::cerr << "[YRM1001] ERROR: Invalid tail (0x"
                  << std::hex << static_cast<int>(m_rawBuffer[m_bufferPos - 1]) << std::dec << ")" << std::endl;
        return false;
    }
    return true; 
}


bool C_YRM1001::parseFrame(char* epcOut, size_t epcSize) const {
    // Parse inventory notification frame.
    if (m_bufferPos < 10) {
        return false;
    }

    if (m_rawBuffer[YRM_IDX_TYPE] != YRM_TYPE_NOTIF ||
        m_rawBuffer[YRM_IDX_COMMAND] != YRM_CMD_INVENTORY) {
        return false;
    }

    uint16_t payloadLen = (m_rawBuffer[YRM_IDX_PL_MSB] << 8) | m_rawBuffer[YRM_IDX_PL_LSB];

    if (payloadLen < 5) {
        std::cerr << "[YRM1001] ERROR: Payload too small" << std::endl;
        return false;
    }

    int epcLen = payloadLen - 5;
    int epcStartIdx = YRM_IDX_PAYLOAD + 1 + 2;  

    // Ensure EPC fits in output buffer.
    if (epcLen * 2 >= static_cast<int>(epcSize)) {
        std::cerr << "[YRM1001] ERROR: EPC too large (" << epcLen << " bytes)" << std::endl;
        return false;
    }

    int checksumIdx = 5 + payloadLen;

    uint8_t expectedCS = calculateChecksum(&m_rawBuffer[YRM_IDX_TYPE], 4 + payloadLen);

    // Warn on checksum mismatch (but continue).
    if (m_rawBuffer[checksumIdx] != expectedCS) {
        std::cerr << "[YRM1001] WARNING: Invalid checksum "
                  << "(expected: 0x" << std::hex << static_cast<int>(expectedCS)
                  << ", received: 0x" << static_cast<int>(m_rawBuffer[checksumIdx])
                  << std::dec << ")" << std::endl;
    }

    bytesToHex(&m_rawBuffer[epcStartIdx], epcLen, epcOut);

    return true;
}

bool C_YRM1001::setPower(uint16_t powerCentiDbm) {
    // Set RF output power (centi-dBm).
    uint8_t cmd_power[] = {
        0xBB, 0x00, 0xB6, 0x00, 0x02,
        static_cast<uint8_t>((powerCentiDbm >> 8) & 0xFF),
        static_cast<uint8_t>(powerCentiDbm & 0xFF),
        0x00, 0x7E
    };

    cmd_power[7] = calculateChecksum(&cmd_power[1], 6);

    std::cout << "[YRM1001] Setting power to: " << (powerCentiDbm / 100.0f) << " dBm\n";

    flushUART();
    if (!sendCommand(cmd_power, sizeof(cmd_power))) return false;

    // Read and validate response frame.
    if (!readFrame()) return false;

    bool ok = false;
    if (m_rawBuffer[YRM_IDX_HEADER] == YRM_HEADER &&
        m_rawBuffer[YRM_IDX_TYPE]   == 0x01 &&
        m_rawBuffer[YRM_IDX_COMMAND]== 0xB6) {

        uint16_t pl = (static_cast<uint16_t>(m_rawBuffer[YRM_IDX_PL_MSB]) << 8) | m_rawBuffer[YRM_IDX_PL_LSB];
        if (pl == 1 && m_rawBuffer[YRM_IDX_PAYLOAD] == 0x00) ok = true;
        }

    if (!ok) return false;

    uint16_t actual{};
    if (getPower(actual)) {
        std::cout << "[YRM1001] Power now: " << (actual / 100.0f) << " dBm\n";
        if (actual != powerCentiDbm) {
            std::cout << "[YRM1001] NOTE: Module clamped/adjusted value (requested "
                      << (powerCentiDbm / 100.0f) << " dBm).\n";
        }
    } else {
        std::cout << "[YRM1001] WARNING: set OK, but getPower() failed.\n";
    }

    return true;
}

bool C_YRM1001::getPower(uint16_t& outCentiDbm) {
    // Query current RF output power.
    uint8_t cmd_get[] = { 0xBB, 0x00, 0xB7, 0x00, 0x00, 0x00, 0x7E };
    cmd_get[5] = calculateChecksum(&cmd_get[1], 4); 

    flushUART();

    if (!sendCommand(cmd_get, sizeof(cmd_get))) return false;
    if (!readFrame()) return false;
    
    if (m_rawBuffer[YRM_IDX_HEADER] != YRM_HEADER) return false;
    if (m_rawBuffer[YRM_IDX_TYPE]   != 0x01)       return false;
    if (m_rawBuffer[YRM_IDX_COMMAND]!= 0xB7)       return false;

    uint16_t pl = (static_cast<uint16_t>(m_rawBuffer[YRM_IDX_PL_MSB]) << 8) | m_rawBuffer[YRM_IDX_PL_LSB];
    if (pl != 2) return false;

    const int payloadIdx  = YRM_IDX_PAYLOAD;      
    const int checksumIdx = 5 + pl;               
    const uint8_t expected = calculateChecksum(&m_rawBuffer[YRM_IDX_TYPE], 4 + pl);

    if (m_rawBuffer[checksumIdx] != expected) {
        // Checksum mismatch.
        return false;
    }
    if (m_rawBuffer[checksumIdx + 1] != YRM_TAIL) return false;

    outCentiDbm = (static_cast<uint16_t>(m_rawBuffer[payloadIdx]) << 8) | m_rawBuffer[payloadIdx + 1];
    return true;
}


uint8_t C_YRM1001::calculateChecksum(const uint8_t* data, size_t len) {
    // Sum checksum over the frame bytes.
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }

    return sum & 0xFF;  
}


void C_YRM1001::bytesToHex(const uint8_t* data, size_t len, char* hexOut) {
    // Convert bytes to ASCII hex string.
    static const char hexChars[] = "0123456789ABCDEF";

    for (size_t i = 0; i < len; i++) {
        hexOut[i * 2]     = hexChars[data[i] >> 4];      
        hexOut[i * 2 + 1] = hexChars[data[i] & 0x0F];    
    }

    hexOut[len * 2] = '\0';  
}


bool C_YRM1001::isTagSeen(const char* epc, char tagList[][32], int tagCount){
    // Deduplicate tags within a scan.
    for (int i = 0; i < tagCount; i++) {
        if (strcmp(tagList[i], epc) == 0) {
            return true;  
        }
    }
    return false;  
}


bool C_YRM1001::read(SensorData* data) {
    if (!data) return false;

    // Use a monotonic timer for scan windows.
    auto nowMs = []() -> int64_t {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    };

    // Scan parameters.
    static constexpr int IDLE_NEW_TAG_MS = 500;   
    static constexpr int TOTAL_SCAN_MS   = 3000;  
    static constexpr int POLL_SLICE_MS   = 100;   

    powerOn();
    std::cout << "[YRM1001] ========== START SCAN ==========\n";

    flushUART();

    setPower(5);

    if (!sendCommand(CMD_START_INVENTORY, sizeof(CMD_START_INVENTORY))) {
        powerOff();
        return false;
    }
    std::cout << "[YRM1001] START command sent\n";

    struct pollfd pfd{};
    pfd.fd = m_uart.getFd();
    pfd.events = POLLIN;

    int tagCount = 0;
    int64_t tStart   = nowMs();
    int64_t tLastNew = tStart;
    
    data->type = ID_YRM1001;
    data->data.rfid_inventory.tagCount = 0;
    for (int i = 0; i < MAX_TAGS; ++i) {
        data->data.rfid_inventory.tagList[i][0] = '\0';
    }
    while (tagCount < MAX_TAGS) {
        // Stop if total scan time elapsed.
        if (nowMs() - tStart >= TOTAL_SCAN_MS) {
            std::cout << "[YRM1001] Total scan timeout (" << TOTAL_SCAN_MS << "ms)\n";
            break;
        }

        // Stop if no new tags appear for a while.
        if (nowMs() - tLastNew >= IDLE_NEW_TAG_MS) {
            std::cout << "[YRM1001] No new tags for " << IDLE_NEW_TAG_MS << "ms -> done\n";
            break;
        }

        int ret = poll(&pfd, 1, POLL_SLICE_MS);

        if (ret == 0) {
            // No data yet.
            continue;
        }

        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[YRM1001] ERROR: poll() failed\n";
            break;
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            std::cerr << "[YRM1001] ERROR: UART revents=" << std::hex << pfd.revents << std::dec << "\n";
            break;
        }

        if (pfd.revents & POLLIN) {
            if (!readFrame()) {
                // Ignore malformed frame.
                continue;
            }

            char epc[32];
            if (!parseFrame(epc, sizeof(epc))) {
                continue;
            }

            // Add unique tags only.
            if (!isTagSeen(epc, data->data.rfid_inventory.tagList, tagCount)) {
                // Store EPC string.
                std::strncpy(data->data.rfid_inventory.tagList[tagCount], epc, 31);
                data->data.rfid_inventory.tagList[tagCount][31] = '\0';

                tagCount++;
                tLastNew = nowMs(); 

                std::cout << "[YRM1001] Tag " << tagCount << ": " << epc << "\n";
            }
        }
    }
    sendCommand(CMD_STOP_INVENTORY, sizeof(CMD_STOP_INVENTORY));

    powerOff();

    data->data.rfid_inventory.tagCount = tagCount;

    std::cout << "[YRM1001] ========== END SCAN ==========\n";
    std::cout << "[YRM1001] Total tags found: " << tagCount << "\n";

    return true;
}
