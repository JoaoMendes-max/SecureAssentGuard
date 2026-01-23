#include "C_TH_SHT30.h"
#include "C_I2C.h"
#include <iostream>


#define CMD_MSB 0x2C
#define CMD_LSB 0x06

C_TH_SHT30::C_TH_SHT30(C_I2C& i2c)
    : C_Sensor(ID_SHT31), m_i2c(i2c)
{
}

C_TH_SHT30::~C_TH_SHT30() = default;

bool C_TH_SHT30::init() {

    if (!m_i2c.init()) {
        cerr << "[SHT30] Falha: Não foi possível inicializar o barramento I2C." << endl;
        return false;
    }

    return true;
}

bool C_TH_SHT30::read(SensorData* data) {
    

    
    if (!m_i2c.writeRegister(CMD_MSB, CMD_LSB)) {
        std::cerr << "[SHT30] ERRO: Falha ao enviar comando" << std::endl;
        return false;
    }

    uint8_t buffer[6];
    if (m_i2c.readRaw(buffer, 6) != 6) {
        std::cerr << "[SHT30] ERRO: Falha ao ler dados (sensor não respondeu)"
                 << std::endl;
        return false;
    }

    if (calculateCRC(buffer, 2) != buffer[2]) {
        std::cerr << "[SHT30] ERRO: CRC temperatura inválido" << std::endl;
        return false;
    }

    if (calculateCRC(buffer + 3, 2) != buffer[5]) {
        std::cerr << "[SHT30] ERRO: CRC humidade inválido" << std::endl;
        return false;
    }

    uint16_t rawTemp = (buffer[0] << 8) | buffer[1];
    float temp = -45.0f + 175.0f * (static_cast<float>(rawTemp) / 65535.0f);

    uint16_t rawHum = (buffer[3] << 8) | buffer[4];
    float hum = 100.0f * (static_cast<float>(rawHum) / 65535.0f);

    if (data) {
        data->type = ID_SHT31;
        data->data.tempHum.temp = temp;
        data->data.tempHum.hum = hum;
    }

    return true;
}

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
