#ifndef C_I2C_H
#define C_I2C_H

/*
 * I2C abstraction (device file + slave ioctl).
 */

#include <string>
#include <cstdint> 
using namespace std;


class C_I2C {
    uint8_t m_slaveaddress;
    int m_fd;
    string m_devicePath;
public:
    C_I2C(int i2cbusnum, uint8_t slave_address);
    ~C_I2C();
    bool init();
    void closeI2C();
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegister(uint8_t reg, uint8_t& value);
    bool readBytes(uint8_t reg, uint8_t* buffer, size_t len);
    ssize_t readRaw(uint8_t* buffer, size_t len);
};

#endif 
