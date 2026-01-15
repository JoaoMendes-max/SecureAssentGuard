#include <poll.h>
#include "C_Fingerprint.h"
#include "C_UART.h"
#include "C_GPIO.h"
#include <iostream>
#include <unistd.h> // required for usleep()

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

void C_Fingerprint::wakeUp() {
    m_rst.writePin(true);     // Set RST Pin HIGH to wake up the sensor
    usleep(250000);     //Wait 250ms for the sensor to boot up before sending commands
}

void C_Fingerprint::sleep() {
    m_rst.writePin(false);     // Set RST Pin LOW to put sensor to sleep (save power)
}

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

bool C_Fingerprint::addUser(int userID) {
    uint8_t p1 = (userID >> 8) & 0xFF; // High Byte of ID
    uint8_t p2 = userID & 0xFF;        // Low Byte of ID
    uint8_t perm = 1;                  // Default Permission

    // Use a long timeout (10s) for each step
    // to give the user time to place/lift finger

    std::cout << "[Finger] Step 1/3: Place finger..." << std::endl;
    if (executeCommand(CMD_ADD_1, p1, p2, perm, nullptr,
        nullptr, 10.0) != ACK_SUCCESS) return false;

    std::cout << "[Finger] Step 2/3: Place finger again..." << std::endl;
    if (executeCommand(CMD_ADD_2, p1, p2, perm, nullptr,
        nullptr, 10.0) != ACK_SUCCESS) return false;

    std::cout << "[Finger] Step 3/3: Final confirmation..." << std::endl;
    if (executeCommand(CMD_ADD_3, p1, p2, perm, nullptr,
        nullptr, 10.0) != ACK_SUCCESS) return false;

    return true;
}

bool C_Fingerprint::deleteUser(int userID) {
    uint8_t p1 = (userID >> 8) & 0xFF;
    uint8_t p2 = userID & 0xFF;

    // Short timeout (1s) is enough for deletion
    // as it doesn't require finger interaction
    return (executeCommand(CMD_DEL, p1, p2, 0, nullptr,
        nullptr, 1.0) == ACK_SUCCESS);
}

uint8_t C_Fingerprint::executeCommand(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3,
    uint8_t* outHigh, uint8_t* outLow, float timeoutSec) const {

    std::cout << "[DEBUG] >> A entrar em executeCommand (POLL VERSION)..." << std::endl;

    // 1. FLUSH BUFFER
    std::cout << "[DEBUG] A limpar buffer (Flush)..." << std::endl;
    uint8_t trash[64];
    int flushCount = 0;
    while(m_uart.readBuffer(trash, 64) > 0) {
        flushCount++;
        if (flushCount > 10) {
            std::cout << "[ERRO] Loop infinito no Flush!" << std::endl;
            break;
        }
    }
    if (flushCount > 0) std::cout << "[DEBUG] Lixo limpo." << std::endl;

    // 2. BUILD COMMAND PACKET
    uint8_t tx[8];
    tx[0] = FINGER_HEAD;
    tx[1] = cmd;
    tx[2] = p1; tx[3] = p2; tx[4] = p3;
    tx[5] = 0;
    tx[6] = tx[1] ^ tx[2] ^ tx[3] ^ tx[4] ^ tx[5]; // Checksum
    tx[7] = FINGER_TAIL;

    std::cout << "[DEBUG] A enviar comando..." << std::endl;
    m_uart.writeBuffer(tx, 8);

    // 3. RECEIVE RESPONSE USING POLL()
    std::cout << "[DEBUG] A esperar resposta (Poll)..." << std::endl;
    uint8_t rx[8];
    int totalRead = 0;
    struct pollfd pfd;
    pfd.fd = m_uart.getFd();
    pfd.events = POLLIN;     // we wanna read data
    int timeoutMs = (int)(timeoutSec * 1000);

    while (totalRead < 8) {

        int ret = poll(&pfd, 1, timeoutMs);

        if (ret > 0) {
            if (pfd.revents & POLLIN) {
                int n = m_uart.readBuffer(rx + totalRead, 8 - totalRead);
                if (n > 0) {
                    totalRead += n;
                    std::cout << "[DEBUG] Recebi " << n << " bytes. Total: " << totalRead << "/8" << std::endl;
                }
            }
        }
        else if (ret == 0) {
            // TIMEOUT
            return ACK_TIMEOUT;
        }
        else {
            std::cout << "[ERRO] Erro critico no Poll!" << std::endl;
            return ACK_TIMEOUT;
        }
    }

    std::cout << "[DEBUG] Pacote completo recebido." << std::endl;

    // 4. VALIDATE
    if (rx[0] != FINGER_HEAD || rx[7] != FINGER_TAIL) {
        std::cout << "[DEBUG] Erro de Frame (Head/Tail incorretos)" << std::endl;
        return 0xFF;
    }

    uint8_t chk = rx[1] ^ rx[2] ^ rx[3] ^ rx[4] ^ rx[5];
    if (rx[6] != chk) {
        std::cout << "[DEBUG] Erro de Checksum (Calculado: " << (int)chk << " vs Recebido: " << (int)rx[6] << ")" << std::endl;
        return 0xFF;
    }

    // 5. EXTRACT DATA
    if (outHigh) *outHigh = rx[2];
    if (outLow)  *outLow  = rx[3];

    std::cout << "[DEBUG] Resposta valida! Status (Q3): " << (int)rx[4] << std::endl;
    return rx[5];

}