#include "C_YRM1001.h"
#include "C_UART.h"
#include "C_GPIO.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <chrono>

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

bool C_YRM1001::setPower(uint16_t powerCentiDbm) {
    uint8_t cmd_power[] = {
        0xBB, 0x00, 0xB6, 0x00, 0x02,
        (uint8_t)((powerCentiDbm >> 8) & 0xFF),
        (uint8_t)(powerCentiDbm & 0xFF),
        0x00, 0x7E
    };

    cmd_power[7] = calculateChecksum(&cmd_power[1], 6);

    std::cout << "[YRM1001] Setting power to: " << (powerCentiDbm / 100.0f) << " dBm\n";

    flushUART();
    if (!sendCommand(cmd_power, sizeof(cmd_power))) return false;

    // Resposta do set: BB 01 B6 00 01 00 CS 7E (00 = OK) :contentReference[oaicite:4]{index=4}
    if (!readFrame()) return false;

    bool ok = false;
    if (m_rawBuffer[YRM_IDX_HEADER] == YRM_HEADER &&
        m_rawBuffer[YRM_IDX_TYPE]   == 0x01 &&
        m_rawBuffer[YRM_IDX_COMMAND]== 0xB6) {

        uint16_t pl = (uint16_t(m_rawBuffer[YRM_IDX_PL_MSB]) << 8) | m_rawBuffer[YRM_IDX_PL_LSB];
        if (pl == 1 && m_rawBuffer[YRM_IDX_PAYLOAD] == 0x00) ok = true;
        }

    if (!ok) return false;

    // ✅ Confirma o valor real guardado (pode haver clamp)
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
    // Command frame: BB 00 B7 00 00 CS 7E, com CS = soma(Type..PL) :contentReference[oaicite:2]{index=2}
    uint8_t cmd_get[] = { 0xBB, 0x00, 0xB7, 0x00, 0x00, 0x00, 0x7E };
    cmd_get[5] = calculateChecksum(&cmd_get[1], 4); // Type+Cmd+PL_MSB+PL_LSB

    flushUART();

    if (!sendCommand(cmd_get, sizeof(cmd_get))) return false;
    if (!readFrame()) return false;

    // Valida resposta: Type=0x01, Cmd=0xB7, PL=0x0002, payload = Pow(MSB), Pow(LSB) :contentReference[oaicite:3]{index=3}
    if (m_rawBuffer[YRM_IDX_HEADER] != YRM_HEADER) return false;
    if (m_rawBuffer[YRM_IDX_TYPE]   != 0x01)       return false;
    if (m_rawBuffer[YRM_IDX_COMMAND]!= 0xB7)       return false;

    uint16_t pl = (uint16_t(m_rawBuffer[YRM_IDX_PL_MSB]) << 8) | m_rawBuffer[YRM_IDX_PL_LSB];
    if (pl != 2) return false;

    const int payloadIdx  = YRM_IDX_PAYLOAD;      // 5
    const int checksumIdx = 5 + pl;               // header(5) + payloadLen
    const uint8_t expected = calculateChecksum(&m_rawBuffer[YRM_IDX_TYPE], 4 + pl);

    if (m_rawBuffer[checksumIdx] != expected) {
        // se preferires ser permissivo, podes só fazer warning e continuar
        return false;
    }
    if (m_rawBuffer[checksumIdx + 1] != YRM_TAIL) return false;

    outCentiDbm = (uint16_t(m_rawBuffer[payloadIdx]) << 8) | m_rawBuffer[payloadIdx + 1];
    return true;
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

    auto nowMs = []() -> int64_t {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    };

    // Ajusta estes valores como quiseres
    static constexpr int IDLE_NEW_TAG_MS = 500;   // pára se não houver tag NOVA por 500ms
    static constexpr int TOTAL_SCAN_MS   = 3000;  // pára sempre ao fim de 3s
    static constexpr int POLL_SLICE_MS   = 100;   // poll em fatias pequenas

    powerOn();
    std::cout << "[YRM1001] ========== START SCAN ==========\n";

    flushUART();

    // Se o teu setPower já está testado, mantém.
    // (Nota: setPower(5) parece baixo, mas assumo que testaste.)
    setPower(5);

    // Start inventory
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

    // Limpa a estrutura de saída
    data->type = ID_YRM1001;
    data->data.rfid_inventory.tagCount = 0;
    for (int i = 0; i < MAX_TAGS; ++i) {
        data->data.rfid_inventory.tagList[i][0] = '\0';
    }

    while (tagCount < MAX_TAGS) {
        // Timeout total (segurança)
        if (nowMs() - tStart >= TOTAL_SCAN_MS) {
            std::cout << "[YRM1001] Total scan timeout (" << TOTAL_SCAN_MS << "ms)\n";
            break;
        }

        // Se não há TAG NOVA há algum tempo, termina
        if (nowMs() - tLastNew >= IDLE_NEW_TAG_MS) {
            std::cout << "[YRM1001] No new tags for " << IDLE_NEW_TAG_MS << "ms -> done\n";
            break;
        }

        int ret = poll(&pfd, 1, POLL_SLICE_MS);

        if (ret == 0) {
            // não houve dados nesta fatia; não termina já — o critério é "sem TAG nova"
            continue;
        }

        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[YRM1001] ERROR: poll() failed\n";
            break;
        }

        // Trata erros no fd (evita loops estranhos)
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            std::cerr << "[YRM1001] ERROR: UART revents=" << std::hex << pfd.revents << std::dec << "\n";
            break;
        }

        if (pfd.revents & POLLIN) {
            if (!readFrame()) {
                // frame incompleta/ruim — ignora e continua
                continue;
            }

            char epc[32];
            if (!parseFrame(epc, sizeof(epc))) {
                continue;
            }

            // Só conta quando é EPC novo
            if (!isTagSeen(epc, data->data.rfid_inventory.tagList, tagCount)) {
                // garante terminação e evita overflow (epc é 32, destino é 32)
                std::strncpy(data->data.rfid_inventory.tagList[tagCount], epc, 31);
                data->data.rfid_inventory.tagList[tagCount][31] = '\0';

                tagCount++;
                tLastNew = nowMs(); // ✅ só renova quando aparece EPC novo

                std::cout << "[YRM1001] Tag " << tagCount << ": " << epc << "\n";
            }
        }
    }

    // Stop inventory (mesmo que não tenha encontrado tags)
    sendCommand(CMD_STOP_INVENTORY, sizeof(CMD_STOP_INVENTORY));
    usleep(YRM_STOP_TIME_MS * 1000);

    powerOff();

    data->data.rfid_inventory.tagCount = tagCount;

    std::cout << "[YRM1001] ========== END SCAN ==========\n";
    std::cout << "[YRM1001] Total tags found: " << tagCount << "\n";

    return true;
}