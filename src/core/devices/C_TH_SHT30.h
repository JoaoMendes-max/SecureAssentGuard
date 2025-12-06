#ifndef C_TH_SHT30_H
#define C_TH_SHT30_H

#include "C_Sensor.h"
#include "C_I2C.h"
#include <stdint.h>

#define SHT30_ADDR 0x44

class C_TH_SHT30 final : public C_Sensor {
private:
    C_I2C& m_i2c;
    // Função auxiliar para validar dados
    static uint8_t calculateCRC(const uint8_t* data, size_t len);

public:
    C_TH_SHT30(C_I2C& i2c);
    ~C_TH_SHT30() override;

    bool init() override;
    bool read(SensorData* data) override;
};

#endif