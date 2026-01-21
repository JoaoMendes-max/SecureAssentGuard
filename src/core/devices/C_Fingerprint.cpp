#include <poll.h>
#include "C_Fingerprint.h"
#include "C_UART.h"
#include "C_GPIO.h"
#include <iostream>
#include <unistd.h> 

C_Fingerprint::C_Fingerprint(C_UART& uart, C_GPIO& rst)
    : C_Sensor(ID_FINGERPRINT), m_uart(uart), m_rst(rst)
{
}

C_Fingerprint::~C_Fingerprint() = default;

bool C_Fingerprint::init() {
    
    
    if (!m_rst.init()) return false;
    if (!m_uart.openPort()) return false;
    if (!m_uart.configPort(19200, 8, 'N')) return false; 
    return true;
}

void C_Fingerprint::wakeUp() {
    m_rst.writePin(true);     
    usleep(250000);     
}

void C_Fingerprint::sleep() {
    m_rst.writePin(false);     
}

bool C_Fingerprint::read(SensorData* data) {
    uint8_t idHigh = 0, idLow = 0;


    const uint8_t status = executeCommand(CMD_MATCH, 0, 0, 0, &idHigh, &idLow, 5.0);

    if (data) {
        data->type = ID_FINGERPRINT;
        
        if (status == 1 || status == 2 || status == 3) {
            data->data.fingerprint.authenticated = true;
            data->data.fingerprint.userID = (idHigh << 8) | idLow;
            std::cout << "[Finger] User ID Verified: " << data->data.fingerprint.userID << std::endl;
        } else {
            
            data->data.fingerprint.authenticated = false;
            data->data.fingerprint.userID = -1;
        }
    }
    
    return (status != ACK_TIMEOUT);
}

bool C_Fingerprint::addUser(const int userID) const {
    const uint8_t p1 = (userID >> 8) & 0xFF;
    const uint8_t p2 = userID & 0xFF;
    const uint8_t perm = 1;

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

bool C_Fingerprint::deleteUser(const int userID) const {
    uint8_t p1 = (userID >> 8) & 0xFF;
    uint8_t p2 = userID & 0xFF;

    return (executeCommand(CMD_DEL, p1, p2, 0, nullptr,
        nullptr, 1.0) == ACK_SUCCESS);
}

uint8_t C_Fingerprint::executeCommand(const uint8_t cmd, const uint8_t p1, const uint8_t p2, const uint8_t p3,
    uint8_t* outHigh, uint8_t* outLow, const float timeoutSec) const {

    std::cout << "[DEBUG] >> A entrar em executeCommand (POLL VERSION)..." << std::endl;

    
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

    
    uint8_t tx[8];
    tx[0] = FINGER_HEAD;
    tx[1] = cmd;
    tx[2] = p1; tx[3] = p2; tx[4] = p3;
    tx[5] = 0;
    tx[6] = tx[1] ^ tx[2] ^ tx[3] ^ tx[4] ^ tx[5]; 
    tx[7] = FINGER_TAIL;

    std::cout << "[DEBUG] A enviar comando..." << std::endl;
    m_uart.writeBuffer(tx, 8);

    std::cout << "[DEBUG] A esperar resposta (Poll)..." << std::endl;
    uint8_t rx[8];
    int totalRead = 0;
    struct pollfd pfd;
    pfd.fd = m_uart.getFd();
    pfd.events = POLLIN;     
    // int timeoutMs = (int)(timeoutSec * 1000);
    int timeoutMs = static_cast<int>(timeoutSec * 1000);

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
            
            return ACK_TIMEOUT;
        }
        else {
            std::cout << "[ERRO] Erro critico no Poll!" << std::endl;
            return ACK_TIMEOUT;
        }
    }

    std::cout << "[DEBUG] Pacote completo recebido." << std::endl;

    
    if (rx[0] != FINGER_HEAD || rx[7] != FINGER_TAIL) {
        std::cout << "[DEBUG] Erro de Frame (Head/Tail incorretos)" << std::endl;
        return 0xFF;
    }

    uint8_t chk = rx[1] ^ rx[2] ^ rx[3] ^ rx[4] ^ rx[5];
    if (rx[6] != chk) {
        // std::cout << "[DEBUG] Erro de Checksum (Calculado: " << (int)chk << " vs Recebido: " << (int)rx[6] << ")" << std::endl;
        std::cout << "[DEBUG] Erro de Checksum (Calculado: " << static_cast<int>(chk) << " vs Recebido: " << static_cast<int>(rx[6]) << ")" << std::endl;
        return 0xFF;
    }

    
    if (outHigh) *outHigh = rx[2];
    if (outLow)  *outLow  = rx[3];

    // std::cout << "[DEBUG] Resposta valida! Status (Q3): " << (int)rx[4] << std::endl;
    std::cout << "[DEBUG] Resposta valida! Status (Q3): " << static_cast<int>(rx[4]) << std::endl;
    return rx[4];

}

static constexpr uint8_t CMD_DELETE_ALL = 0x05;

bool C_Fingerprint::deleteAllUsers() const {
    
    uint8_t st = executeCommand(CMD_DELETE_ALL, 0, 0, 0, nullptr, nullptr, 2.0f);
    return (st == ACK_SUCCESS);
}
