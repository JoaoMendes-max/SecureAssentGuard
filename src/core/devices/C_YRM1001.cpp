#include "C_YRM1001.h"
#include "C_UART.h"
#include "C_GPIO.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <poll.h>

/* ============================================
 * COMMAND DEFINITIONS
 * Protocol frames for YRM1001 module
 * ============================================ */
static const uint8_t CMD_START_INVENTORY[] = {
    0xBB, 0x00, 0x27, 0x00, 0x03, 0x22, 0xFF, 0xFF, 0x4A, 0x7E
};

static const uint8_t CMD_STOP_INVENTORY[] = {
    0xBB, 0x00, 0x28, 0x00, 0x00, 0x28, 0x7E
};

/* ============================================
 * CONSTRUCTOR / DESTRUCTOR
 * ============================================ */
C_YRM1001::C_YRM1001(C_UART& uart, C_GPIO& enable)
    : C_Sensor(ID_YRM1001),
      m_uart(uart),
      m_gpio_enable(enable),
      m_bufferPos(0)
{
    // Initialize buffer to zero
    std::memset(m_rawBuffer, 0, sizeof(m_rawBuffer));
}

C_YRM1001::~C_YRM1001() {
    powerOff();
}

/* ============================================
 * INIT
 * Initialize UART and GPIO hardware
 * ============================================ */
bool C_YRM1001::init() {
    // Initialize GPIO Enable pin
    if (!m_gpio_enable.init()) {
        std::cerr << "[YRM1001] ERROR: Failed to initialize GPIO Enable" << std::endl;
        return false;
    }

    // Initialize UART port
    if (!m_uart.openPort()) {
        std::cerr << "[YRM1001] ERROR: Failed to open UART" << std::endl;
        return false;
    }

    // Configure UART: 115200 baud, 8 data bits, No parity
    if (!m_uart.configPort(115200, 8, 'N')) {
        std::cerr << "[YRM1001] ERROR: Failed to configure UART (115200 8N1)" << std::endl;
        return false;
    }

    // Ensure module is powered off initially
    powerOff();

    std::cout << "[YRM1001] Initialized (UART 115200, EN ready)" << std::endl;
    return true;
}

/* ============================================
 * POWER MANAGEMENT
 * ============================================ */

// Turn on module power
bool C_YRM1001::powerOn() {
    m_gpio_enable.writePin(true);  // EN = HIGH
    usleep(YRM_BOOT_TIME_MS * 1000);  // Wait for module boot
    std::cout << "[YRM1001] Power ON (waited " << YRM_BOOT_TIME_MS << "ms)" << std::endl;
    return true;
}

// Turn off module power
void C_YRM1001::powerOff() {
    m_gpio_enable.writePin(false);  // EN = LOW
    std::cout << "[YRM1001] Power OFF" << std::endl;
}

/* ============================================
 * FLUSH UART
 * Clear any garbage data in UART buffer
 * ============================================ */
void C_YRM1001::flushUART() const {
    uint8_t trash[64];
    int count = 0;

    // Read and discard up to 10 chunks
    while (m_uart.readBuffer(trash, sizeof(trash)) > 0 && ++count < 10);

    if (count > 0) {
        std::cout << "[YRM1001] Flush: Cleared UART buffer" << std::endl;
    }
}

/* ============================================
 * SEND COMMAND
 * Write command bytes to UART
 * ============================================ */
bool C_YRM1001::sendCommand(const uint8_t* cmd, size_t len) const {
    int written = m_uart.writeBuffer(cmd, len);

    if (written != (int)len) {
        std::cerr << "[YRM1001] ERROR: Failed to send command" << std::endl;
        return false;
    }

    return true;
}

/* ============================================
 * READ FRAME
 * Read complete frame from UART using size-based approach
 * Prevents false detection of 0x7E in EPC data
 * ============================================ */
bool C_YRM1001::readFrame() {
    m_bufferPos = 0;

    struct pollfd pfd;
    pfd.fd = m_uart.getFd();
    pfd.events = POLLIN;

    /* ========== PHASE 1: Read Header (5 bytes) ========== */
    // Frame structure: BB | Type | Cmd | PL_H | PL_L
    const int HEADER_SIZE = 5;

    while (m_bufferPos < HEADER_SIZE) {
        // Wait up to 100ms for data
        int ret = poll(&pfd, 1, 100);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            int n = m_uart.readBuffer(&m_rawBuffer[m_bufferPos], HEADER_SIZE - m_bufferPos);

            if (n > 0) {
                m_bufferPos += n;
            }
        } else if (ret == 0) {
            // Timeout - no data available
            return false;
        } else {
            // Poll error
            std::cerr << "[YRM1001] ERROR: Poll failed on header" << std::endl;
            return false;
        }
    }

    // Validate header byte
    if (m_rawBuffer[YRM_IDX_HEADER] != YRM_HEADER) {
        std::cerr << "[YRM1001] ERROR: Invalid header (0x"
                  << std::hex << (int)m_rawBuffer[0] << std::dec << ")" << std::endl;
        return false;
    }

    /* ========== PHASE 2: Calculate Total Frame Size ========== */
    uint16_t payloadLen = (m_rawBuffer[YRM_IDX_PL_MSB] << 8) | m_rawBuffer[YRM_IDX_PL_LSB];

    // Total frame = Header(5) + Payload(N) + Checksum(1) + Tail(1)
    int totalFrameSize = 5 + payloadLen + 2;

    // Sanity check
    if (totalFrameSize > (int)sizeof(m_rawBuffer)) {
        std::cerr << "[YRM1001] ERROR: Frame too large (" << totalFrameSize << " bytes)" << std::endl;
        return false;
    }

    /* ========== PHASE 3: Read Remaining Data (EFFICIENT) ========== */
    int remaining = totalFrameSize - HEADER_SIZE;

    while (m_bufferPos < totalFrameSize) {
        // Timeout of 100ms (sufficient for burst data)
        int ret = poll(&pfd, 1, 100);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            // Read available data (up to 'remaining' bytes)
            int n = m_uart.readBuffer(&m_rawBuffer[m_bufferPos], remaining);

            if (n > 0) {
                m_bufferPos += n;
                remaining -= n;
            }
        } else if (ret == 0) {
            // Timeout in the middle of frame
            std::cerr << "[YRM1001] ERROR: Timeout mid-frame (got "
                      << m_bufferPos << "/" << totalFrameSize << " bytes)" << std::endl;
            return false;
        } else {
            // Poll error
            std::cerr << "[YRM1001] ERROR: Poll failed on payload" << std::endl;
            return false;
        }
    }

    /* ========== FINAL VALIDATION ========== */
    // Validate tail byte
    if (m_rawBuffer[m_bufferPos - 1] != YRM_TAIL) {
        std::cerr << "[YRM1001] ERROR: Invalid tail (0x"
                  << std::hex << (int)m_rawBuffer[m_bufferPos - 1] << std::dec << ")" << std::endl;
        return false;
    }

    return true; // Complete and valid frame
}

/* ============================================
 * PARSE FRAME
 * Extract EPC from notification frame
 * Validates checksum (corrected: +4 bytes)
 * ============================================ */
bool C_YRM1001::parseFrame(char* epcOut, size_t epcSize) const {
    // Minimum frame size check
    if (m_bufferPos < 10) {
        return false;
    }

    // Verify this is an inventory notification
    if (m_rawBuffer[YRM_IDX_TYPE] != YRM_TYPE_NOTIF ||
        m_rawBuffer[YRM_IDX_COMMAND] != YRM_CMD_INVENTORY) {
        return false;
    }

    // Extract payload length
    uint16_t payloadLen = (m_rawBuffer[YRM_IDX_PL_MSB] << 8) | m_rawBuffer[YRM_IDX_PL_LSB];

    // Payload structure: [RSSI(1)] [PC(2)] [EPC(N)] [CRC(2)]
    // Therefore: EPC_len = PL - 1(RSSI) - 2(PC) - 2(CRC) = PL - 5
    if (payloadLen < 5) {
        std::cerr << "[YRM1001] ERROR: Payload too small" << std::endl;
        return false;
    }

    int epcLen = payloadLen - 5;
    int epcStartIdx = YRM_IDX_PAYLOAD + 1 + 2;  // Skip RSSI(1) + PC(2)

    // Verify EPC fits in output buffer
    if (epcLen * 2 >= (int)epcSize) {  // *2 because each byte becomes 2 hex chars
        std::cerr << "[YRM1001] ERROR: EPC too large (" << epcLen << " bytes)" << std::endl;
        return false;
    }

    /* ========== CHECKSUM VALIDATION (CORRECTED: +4) ========== */
    // Checksum is at: Header(5) + Payload(N)
    int checksumIdx = 5 + payloadLen;

    // Checksum = sum of: Type + Cmd + PL_H + PL_L + Payload
    // Total = 4 fixed bytes + payloadLen
    uint8_t expectedCS = calculateChecksum(&m_rawBuffer[YRM_IDX_TYPE], 4 + payloadLen);

    if (m_rawBuffer[checksumIdx] != expectedCS) {
        std::cerr << "[YRM1001] WARNING: Invalid checksum "
                  << "(expected: 0x" << std::hex << (int)expectedCS
                  << ", received: 0x" << (int)m_rawBuffer[checksumIdx]
                  << std::dec << ")" << std::endl;
        // Continue anyway (some firmwares have checksum bugs)
        // Uncomment below to reject invalid frames:
        // return false;
    }

    /* ========== EPC EXTRACTION ========== */
    // Convert bytes to hexadecimal string
    bytesToHex(&m_rawBuffer[epcStartIdx], epcLen, epcOut);

    return true;
}

bool C_YRM1001::setPower(uint16_t powerDBm) {
    // A maioria dos YRM aceita de 0 a 2600 (centésimos de dBm)
    // ou 0 a 26 (dBm). Vamos assumir dBm simples para este exemplo.

    uint8_t cmd_power[] = {
        0xBB, 0x00, 0xB6, 0x00, 0x02,
        (uint8_t)((powerDBm >> 8) & 0xFF), // MSB
        (uint8_t)(powerDBm & 0xFF),        // LSB
        0x00, 0x7E
    };

    // Calcula o Checksum: Soma de Type(00) + Cmd(B6) + PL_L(00) + PL_H(02) + Payload
    cmd_power[7] = calculateChecksum(&cmd_power[1], 6);

    std::cout << "[YRM1001] Setting power to: " << powerDBm << " dBm" << std::endl;

    if (!sendCommand(cmd_power, sizeof(cmd_power))) return false;

    // Opcional: Ler a resposta do módulo (readFrame) para confirmar sucesso
    if (readFrame()) {
        if (m_rawBuffer[YRM_IDX_COMMAND] == 0xB6 && m_rawBuffer[5] == 0x00) {
            std::cout << "[YRM1001] Power set successfully!" << std::endl;
            return true;
        }
    }
    return false;
}

/* ============================================
 * CALCULATE CHECKSUM
 * Sum all bytes and return LSB
 * ============================================ */
uint8_t C_YRM1001::calculateChecksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }

    return sum & 0xFF;  // LSB only
}

/* ============================================
 * BYTES TO HEX
 * Convert raw bytes to hexadecimal string
 * ============================================ */
void C_YRM1001::bytesToHex(const uint8_t* data, size_t len, char* hexOut) {
    static const char hexChars[] = "0123456789ABCDEF";

    for (size_t i = 0; i < len; i++) {
        hexOut[i * 2]     = hexChars[data[i] >> 4];      // High nibble
        hexOut[i * 2 + 1] = hexChars[data[i] & 0x0F];    // Low nibble
    }

    hexOut[len * 2] = '\0';  // Null terminator
}

/* ============================================
 * IS TAG SEEN
 * Check if EPC already exists in tag list
 * ============================================ */
bool C_YRM1001::isTagSeen(const char* epc, char tagList[][32], int tagCount){
    for (int i = 0; i < tagCount; i++) {
        if (strcmp(tagList[i], epc) == 0) {
            return true;  // Duplicate found
        }
    }
    return false;  // New tag
}

/* ============================================
 * READ (Main Function)
 * Perform inventory scan and collect unique tags
 * Returns true on success (even with 0 tags)
 * ============================================ */
bool C_YRM1001::read(SensorData* data) {
    if (!data) return false;

    std::cout << "[YRM1001] ========== START SCAN ==========" << std::endl;

     /* ========== 2. CLEAR UART BUFFER ========== */
    flushUART();


    setPower(5);

    /* ========== 3. START INVENTORY COMMAND ========== */
    if (!sendCommand(CMD_START_INVENTORY, sizeof(CMD_START_INVENTORY))) {
        powerOff();
        return false;
    }
    std::cout << "[YRM1001] START command sent (RF scan initiated)" << std::endl;

    /* ========== 4. COLLECT TAGS WITH POLL ========== */
    struct pollfd pfd;
    pfd.fd = m_uart.getFd();
    pfd.events = POLLIN;

    int tagCount = 0;

    while (tagCount < MAX_TAGS) {
        // Poll with 500ms timeout (wait for next tag)
        int ret = poll(&pfd, 1, YRM_IDLE_TIMEOUT_MS);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            // Data available - read complete frame
            if (readFrame()) {
                char epc[32];

                if (parseFrame(epc, sizeof(epc))) {
                    // Check if we already have this tag (duplicate)
                    if (!isTagSeen(epc, data->data.rfid_inventory.tagList, tagCount)) {
                        // New tag - add to list
                        strcpy(data->data.rfid_inventory.tagList[tagCount], epc);
                        tagCount++;

                        std::cout << "[YRM1001] Tag " << tagCount << ": " << epc << std::endl;
                    }
                    // Silently ignore duplicates and continue
                }
            }
        } else if (ret == 0) {
            // Timeout: 500ms without data = RF scan complete
            std::cout << "[YRM1001] Timeout (" << YRM_IDLE_TIMEOUT_MS
                      << "ms without data) - Scan complete" << std::endl;
            break;
        } else {
            // Poll error
            std::cerr << "[YRM1001] ERROR: Poll failed during collection" << std::endl;
            break;
        }
    }

    /* ========== 5. STOP INVENTORY COMMAND ========== */
    sendCommand(CMD_STOP_INVENTORY, sizeof(CMD_STOP_INVENTORY));
    std::cout << "[YRM1001] STOP command sent" << std::endl;

    // Wait for module to process STOP command (required by specification)
    usleep(YRM_STOP_TIME_MS * 1000);

    /* ========== 6. POWER OFF ========== */
    powerOff();

    /* ========== 7. RESULT ========== */
    data->type = ID_YRM1001;
    data->data.rfid_inventory.tagCount = tagCount;

    std::cout << "[YRM1001] ========== END SCAN ==========" << std::endl;
    std::cout << "[YRM1001] Total tags found: " << tagCount << std::endl;

    return true;  // Success (even with 0 tags)
}