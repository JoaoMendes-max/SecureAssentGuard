#include "C_Fingerprint.h"
#include <iostream>
#include <unistd.h> // required for usleep()
#include <cstring>  // required for memset()

C_Fingerprint::C_Fingerprint(C_UART& uart, C_GPIO& rst)
    : C_Sensor(ID_FINGERPRINT), m_uart(uart), m_rst(rst)
{
}

C_Fingerprint::~C_Fingerprint() {}

bool C_Fingerprint::init() {
    // Initialize hardware drivers only.
    // Actual sensor state (Sleep/Wake) is managed by the thread loop.
    if (!m_rst.init()) return false;
    if (!m_uart.openPort()) return false;
    if (!m_uart.configPort(19200, 8, 'N')) return false; // Sensor requires 19200 baud
    return true;
}

// --- POWER MANAGEMENT ---
void C_Fingerprint::wakeUp() {
    m_rst.writePin(true);     // Set RST Pin HIGH to wake up the sensor
    usleep(250000);     // CRITICAL: Wait 250ms for the sensor to boot up before sending commands
}

void C_Fingerprint::sleep() {
    m_rst.writePin(false);     // Set RST Pin LOW to put sensor to sleep (save power)
}

// --- READ / VERIFY ---
bool C_Fingerprint::read(SensorData* data) {
    uint8_t idHigh = 0, idLow = 0;

    // Send MATCH command with a 5-second timeout
    // This allows time for the sensor to process the image if the finger is already placed
    uint8_t status = executeCommand(CMD_MATCH, 0, 0, 0, &idHigh, &idLow, 5.0);

    if (data) {
        data->type = ID_FINGERPRINT;

        // Status 1, 2, or 3 indicates Success (it represents the User Permission level)
        if (status == 1 || status == 2 || status == 3) {
            data->data.fingerprint.authenticated = true;
            data->data.fingerprint.userID = (idHigh << 8) | idLow;
            std::cout << "[Finger] User ID Verified: " << data->data.fingerprint.userID << std::endl;
        } else {
            // Status 0x05 (NO_USER) or 0x08 (TIMEOUT) means failure
            data->data.fingerprint.authenticated = false;
            data->data.fingerprint.userID = -1;
        }
    }

    // Return true if communication was successful, even if authentication failed
    return (status != ACK_TIMEOUT);
}

// --- ADD USER ---
bool C_Fingerprint::addUser(int userID) {
    uint8_t p1 = (userID >> 8) & 0xFF; // High Byte of ID
    uint8_t p2 = userID & 0xFF;        // Low Byte of ID
    uint8_t perm = 1;                  // Default Permission

    // Use a long timeout (10s) for each step to give the user time to place/lift finger

    std::cout << "[Finger] Step 1/3: Place finger..." << std::endl;
    if (executeCommand(CMD_ADD_1, p1, p2, perm, NULL, NULL, 10.0) != ACK_SUCCESS) return false;

    std::cout << "[Finger] Step 2/3: Place finger again..." << std::endl;
    if (executeCommand(CMD_ADD_2, p1, p2, perm, NULL, NULL, 10.0) != ACK_SUCCESS) return false;

    std::cout << "[Finger] Step 3/3: Final confirmation..." << std::endl;
    if (executeCommand(CMD_ADD_3, p1, p2, perm, NULL, NULL, 10.0) != ACK_SUCCESS) return false;

    return true;
}

// --- DELETE USER ---
bool C_Fingerprint::deleteUser(int userID) {
    uint8_t p1 = (userID >> 8) & 0xFF;
    uint8_t p2 = userID & 0xFF;

    // Short timeout (1s) is enough for deletion as it doesn't require finger interaction
    return (executeCommand(CMD_DEL, p1, p2, 0, NULL, NULL, 1.0) == ACK_SUCCESS);
}

// --- CORE HELPER FUNCTION ---
uint8_t C_Fingerprint::executeCommand(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3, uint8_t* outHigh, uint8_t* outLow, float timeoutSec) {

    // 1. FLUSH BUFFER
    // Critical: Read and discard any old data in the UART buffer to avoid reading "garbage"
    uint8_t trash[64];
    while(m_uart.readBuffer(trash, 64) > 0);

    // 2. BUILD PACKET
    uint8_t tx[8];
    tx[0] = FINGER_HEAD;
    tx[1] = cmd;
    tx[2] = p1; tx[3] = p2; tx[4] = p3;
    tx[5] = 0;
    tx[6] = tx[1] ^ tx[2] ^ tx[3] ^ tx[4] ^ tx[5]; // Calculate Checksum (XOR)
    tx[7] = FINGER_TAIL;

    m_uart.writeBuffer(tx, 8);

    // 3. RECEIVE LOOP WITH TIMEOUT
    uint8_t rx[8];
    int totalRead = 0;

    // Convert timeout seconds to loop iterations (10ms per loop)
    // Example: 5.0s * 100 = 500 iterations
    int maxLoops = (int)(timeoutSec * 100);
    int currentLoop = 0;

    while (totalRead < 8 && currentLoop < maxLoops) {
        int n = m_uart.readBuffer(rx + totalRead, 8 - totalRead);
        if (n > 0) {
            totalRead += n;
        } else {
            usleep(10000); // CRITICAL: Sleep 10ms to yield CPU. Without this, the loop consumes 100% CPU.
            currentLoop++;
        }
    }

    if (totalRead < 8) return ACK_TIMEOUT; // Failed to receive 8 bytes in time

    // 4. VALIDATE RESPONSE
    // Check Headers and Tail
    if (rx[0] != FINGER_HEAD || rx[7] != FINGER_TAIL) return 0xFF;
    // Check Checksum Integrity
    if (rx[6] != (rx[1] ^ rx[2] ^ rx[3] ^ rx[4] ^ rx[5])) return 0xFF;

    // 5. EXTRACT DATA (If requested via pointers)
    if (outHigh) *outHigh = rx[2];
    if (outLow)  *outLow  = rx[3];

    return rx[4]; // Return Status Byte (Q3)
}