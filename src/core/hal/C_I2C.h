#ifndef C_I2C_H
#define C_I2C_H

#include <string>
#include <cstdint> // Para uint8_t
#include <cstddef> // Para size_t
using namespace std;


class C_I2C {
    uint8_t m_slaveaddress;
    int m_fd;
    string m_devicePath;
public:
    C_I2C(int i2cbusnum, uint8_t slave_address);
    ~C_I2C();
    //open dev directory and "set" slave address
    bool init();
    //close fd
    void closeI2C();
    //used mainly to write some configuration in sensor specific register
    bool writeRegister(uint8_t reg, uint8_t value);
    //used to read only one sensor specific reg
    bool readRegister(uint8_t reg, uint8_t& value);
    //used to read more than one reg
    bool readBytes(uint8_t reg, uint8_t* buffer, size_t len);
    // Adicionar em public:
    ssize_t readRaw(uint8_t* buffer, size_t len);
};

#endif // C_I2C_H