#include "C_RDM6300.h"
#include <iostream>
#include <cstring>
#include <unistd.h> // usleep

C_RDM6300::C_RDM6300(C_UART& uart)
    : C_Sensor(ID_RDM6300), m_uart(uart)
{
}

C_RDM6300::~C_RDM6300() {}

bool C_RDM6300::init() {
    if (!m_uart.openPort()) return false;
    if (!m_uart.configPort(9600, 8, 'N')) return false;
    return true;
}

bool C_RDM6300::read(SensorData* data) {
    // 1. Verificar o Primeiro Byte (Cabeçalho)
    char header = 0;
    // Lê apenas 1 byte. Se não houver dados ou não for STX, aborta.
    if (m_uart.readBuffer(&header, 1) != 1) return false;

    if (header != RDM_STX) {
        // Se o primeiro byte não é 0x02, é lixo/erro de sincronização.
        // Limpamos tudo e retornamos falso.
        char trash[32];
        while(m_uart.readBuffer(trash, 32) > 0);
        return false;
    }

    // 2. Ler os restantes 13 bytes (Dados + Checksum + ETX)
    m_rawBuffer[0] = RDM_STX; // Já temos o primeiro
    int bytesToRead = 13;
    int totalRead = 0;
    int attempts = 0;

    // Loop técnico obrigatório: O Linux pode entregar os dados em fatias.
    // Tenta ler o resto do pacote durante ~100ms
    while (totalRead < bytesToRead && attempts < 10) {
        int n = m_uart.readBuffer(m_rawBuffer + 1 + totalRead, bytesToRead - totalRead);
        if (n > 0) {
            totalRead += n;
        } else {
            usleep(10000); // 10ms
            attempts++;
        }
    }

    // Se não leu tudo, aborta
    if (totalRead < bytesToRead) return false;

    // 3. Validações
    bool success = false;
    if (m_rawBuffer[13] == RDM_ETX) {      // Verifica Final
        if (validateChecksum(m_rawBuffer)) { // Verifica Integridade
            success = true;
            if (data) {
                data->type = ID_RDM6300;
                parseTag(m_rawBuffer, data->data.rfid_single.tagID);
            }
        }
    }

    // 4. Flush Final (Obrigatório)
    // O sensor continua a mandar o ID repetidamente.
    // Limpamos o buffer para garantir que a próxima leitura é um evento novo.
    char trash[64];
    while(m_uart.readBuffer(trash, 64) > 0);

    return success;
}

// --- Lógica de Validação (XOR dos valores Hex) ---
bool C_RDM6300::validateChecksum(const char* buffer) {
    uint8_t calcChecksum = 0;

    // Processa os 5 pares de dados (bytes 1 a 10)
    for (int i = 0; i < 5; i++) {
        uint8_t val = hexPairToByte(buffer[1 + (i * 2)], buffer[2 + (i * 2)]);
        calcChecksum ^= val;
    }

    // Lê o checksum recebido (bytes 11 e 12)
    uint8_t receivedChecksum = hexPairToByte(buffer[11], buffer[12]);

    return (calcChecksum == receivedChecksum);
}

// --- Helpers ---
void C_RDM6300::parseTag(const char* buffer, char* dest) {
    // Copia os 10 bytes de dados ASCII para a estrutura
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
