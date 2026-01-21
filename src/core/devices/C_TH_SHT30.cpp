#include "C_TH_SHT30.h"
#include "C_I2C.h"
#include <iostream>

// Comando: Single Shot, High Repeatability, CLOCK STRETCHING ENABLED
#define CMD_MSB 0x2C
#define CMD_LSB 0x06

C_TH_SHT30::C_TH_SHT30(C_I2C& i2c)
    : C_Sensor(ID_SHT31), m_i2c(i2c)
{
}

C_TH_SHT30::~C_TH_SHT30() = default;

// ============================================
// INIT - SEM SLEEP!
// ============================================
bool C_TH_SHT30::init() {

    if (!m_i2c.init()) {
        cerr << "[SHT30] Falha: Não foi possível inicializar o barramento I2C." << endl;
        return false;
    }

    return true;
}

// ============================================
// READ - CLOCK STRETCHING (SEM SLEEP!)
// ============================================
bool C_TH_SHT30::read(SensorData* data) {
    /* ============================================
     * CLOCK STRETCHING - Como funciona:
     *
     * 1. Comando 0x2C06 ativa clock stretching
     * 2. Após recebermos ACK do comando, tentamos LER
     * 3. O SENSOR segura a linha SCL (hardware!)
     * 4. O kernel fica bloqueado no read() (I/O wait)
     * 5. Quando medição completa, sensor liberta SCL
     * 6. read() retorna com os dados
     *
     * ISTO NÃO É SLEEP! É espera de I/O (como ler ficheiro).
     * O scheduler pode executar outras threads enquanto
     * esta está bloqueada no read().
     *
     * Datasheet pág. 9, secção 4.4:
     * "When a command with clock stretching has been issued,
     *  the sensor responds to a read header with an ACK and
     *  subsequently pulls down the SCL line. The SCL line is
     *  pulled down until the measurement is complete."
     * ============================================ */

    // 1. Enviar comando COM clock stretching
    if (!m_i2c.writeRegister(CMD_MSB, CMD_LSB)) {
        std::cerr << "[SHT30] ERRO: Falha ao enviar comando" << std::endl;
        return false;
    }

    // 2. Ler dados IMEDIATAMENTE
    // O read() bloqueia (kernel espera) até sensor libertar SCL
    // Isto NÃO é um sleep - é I/O wait normal!
    uint8_t buffer[6];
    if (m_i2c.readRaw(buffer, 6) != 6) {
        std::cerr << "[SHT30] ERRO: Falha ao ler dados (sensor não respondeu)"
                 << std::endl;
        return false;
    }

    // 3. Validar CRC
    if (calculateCRC(buffer, 2) != buffer[2]) {
        std::cerr << "[SHT30] ERRO: CRC temperatura inválido" << std::endl;
        return false;
    }

    if (calculateCRC(buffer + 3, 2) != buffer[5]) {
        std::cerr << "[SHT30] ERRO: CRC humidade inválido" << std::endl;
        return false;
    }

    // 4. Converter
    uint16_t rawTemp = (buffer[0] << 8) | buffer[1];
    float temp = -45.0f + 175.0f * (static_cast<float>(rawTemp) / 65535.0f);

    uint16_t rawHum = (buffer[3] << 8) | buffer[4];
    float hum = 100.0f * (static_cast<float>(rawHum) / 65535.0f);

    // 5. Guardar
    if (data) {
        data->type = ID_SHT31;
        data->data.tempHum.temp = temp;
        data->data.tempHum.hum = hum;
    }

    return true;
}

// ============================================
// CALCULATE CRC
// ============================================
uint8_t C_TH_SHT30::calculateCRC(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}


/*

#include "C_TH_SHT30.h"
#include "C_I2C.h"
#include <iostream>
#include <unistd.h> // usleep

// Comandos SHT30: High Repeatability, No Clock Stretching
#define CMD_MSB 0x24
#define CMD_LSB 0x00

C_TH_SHT30::C_TH_SHT30(C_I2C& i2c)
    : C_Sensor(ID_SHT31), m_i2c(i2c)
{
}

C_TH_SHT30::~C_TH_SHT30() = default;

bool C_TH_SHT30::init() {
    // O I2C é inicializado fora.
    // Podemos dar um pequeno tempo de boot se o sensor tiver acabado de ligar.
    usleep(20000);
    return true;
}

bool C_TH_SHT30::read(SensorData* data) {
    // 1. Enviar comando "Mede!"
    if (!m_i2c.writeRegister(CMD_MSB, CMD_LSB)) {
        return false;
    }

    // 2. Polling (Perguntar até estar pronto)
    uint8_t buffer[6];
    bool success = false;
    int maxRetries = 50; // Tenta 50 vezes (Max ~50ms)

    for (int i = 0; i < maxRetries; i++) {
        // Tenta ler. Se o sensor estiver a medir, dá erro (NACK).
        if (m_i2c.readRaw(buffer, 6) == 6) {
            success = true;
            break; // Já temos dados!
        }

        // Espera 1ms para não "fritar" o CPU e o barramento
        usleep(1000);
    }

    if (!success) {
        std::cerr << "[SHT30] Timeout: Sensor não responde." << std::endl;
        return false;
    }

    // 3. Validar CRC
    if (calculateCRC(buffer, 2) != buffer[2]) return false;     // Temp CRC
    if (calculateCRC(buffer + 3, 2) != buffer[5]) return false; // Hum CRC

    // 4. Converter
    uint16_t rawTemp = (buffer[0] << 8) | buffer[1];
    float temp = -45.0f + 175.0f * ((float)rawTemp / 65535.0f);

    uint16_t rawHum = (buffer[3] << 8) | buffer[4];
    float hum = 100.0f * ((float)rawHum / 65535.0f);

    // 5. Guardar
    if (data) {
        data->type = ID_SHT31;
        data->data.tempHum.temp = temp;
        data->data.tempHum.hum = hum;
    }

    return true;
}

// Algoritmo CRC-8 padrão da Sensirion
uint8_t C_TH_SHT30::calculateCRC(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc <<= 1;
        }
    }
    return crc;
}

*/