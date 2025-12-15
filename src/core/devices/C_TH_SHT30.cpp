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