#include "C_RDM6300.h"
#include "C_UART.h"
#include <iostream>
#include <cstring>
#include <poll.h>   // [NOVO] Necessário para poll()
#include <unistd.h> // Para close, etc.

C_RDM6300::C_RDM6300(C_UART& uart)
    : C_Sensor(ID_RDM6300), m_uart(uart)
{
}

C_RDM6300::~C_RDM6300() = default;

bool C_RDM6300::init() {
    if (!m_uart.openPort()) return false;
    // Configuração padrão RDM6300: 9600, 8N1
    if (!m_uart.configPort(9600, 8, 'N')) return false;
    return true;
}

// [NOVO] Função auxiliar que usa poll()
// Retorna true se houver dados prontos para ler, false se der timeout
bool C_RDM6300::waitForData(int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = m_uart.getFd(); // [Cite: C_UART.h - getFd() existe]
    pfd.events = POLLIN;     // Queremos saber se há dados para ler

    // poll() bloqueia a thread aqui sem gastar CPU
    // Retorna > 0 (eventos), 0 (timeout), -1 (erro)
    int ret = poll(&pfd, 1, timeout_ms);

    return (ret > 0);
}

bool C_RDM6300::read(SensorData* data) {
    // 1. ESPERA EFICIENTE (Blocking Wait)
    // Espera até 1000ms (1s) pelo início de um pacote.
    // Se não houver cartão, a função retorna false e a thread pode verificar flags de saída.
    if (!waitForData(1000)) {
        return false;
    }

    // 2. Tenta ler o cabeçalho (STX)
    char header = 0;
    // Como o poll disse que há dados, este readBuffer não deve bloquear/falhar
    if (m_uart.readBuffer(&header, 1) != 1) return false;

    if (header != RDM_STX) {
        // Se o primeiro byte não for STX, limpamos o lixo
        char trash[32];
        // Pequeno atraso para garantir que o lixo chegou totalmente antes de mudar
        if (waitForData(10)) {
             while(m_uart.readBuffer(trash, 32) > 0);
        }
        return false;
    }

    // Prepara o buffer
    m_rawBuffer[0] = RDM_STX;
    int bytesToRead = 13; // Faltam 13 bytes (Dados + Checksum + ETX)
    int totalRead = 0;

    // 3. Loop de Leitura do Corpo
    // Aqui usamos um timeout mais curto (100ms) porque se o cabeçalho chegou,
    // o resto da mensagem deve chegar imediatamente.
    while (totalRead < bytesToRead) {

        // Espera pelo próximo pedaço de dados
        if (!waitForData(100)) {
            // Timeout no meio do pacote -> Pacote incompleto/corrompido
            return false;
        }

        int n = m_uart.readBuffer(m_rawBuffer + 1 + totalRead, bytesToRead - totalRead);
        if (n > 0) {
            totalRead += n;
        } else {
            // Erro de leitura mesmo com poll a dizer que havia dados
            return false;
        }
    }

    // 4. Validação (Igual ao anterior)
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

    // Limpeza final do buffer (se houver dados extra indesejados)
    if (waitForData(0)) { // Check não-bloqueante rápido
        char trash[64];
        while(m_uart.readBuffer(trash, 64) > 0);
    }

    return success;
}

// --- Funções Auxiliares mantêm-se iguais ---

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