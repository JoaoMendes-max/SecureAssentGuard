#include "C_I2C.h"
#include <iostream>
#include <fcntl.h>      
#include <unistd.h>     
#include <sys/ioctl.h>  
#include <linux/i2c-dev.h> 

using namespace std;

C_I2C::C_I2C(int i2cbusnum, uint8_t slave_address)
    : m_slaveaddress(slave_address), m_fd(-1)
{

    m_devicePath = "/dev/i2c-" + to_string(i2cbusnum);
}


C_I2C::~C_I2C() {
    closeI2C();
}

bool C_I2C::init() {
    m_fd = open(m_devicePath.c_str(), O_RDWR);
    if (m_fd < 0) {
        cerr << "C_I2C: Erro ao abrir dev/...\n";
        return false;
    }

    if (ioctl(m_fd, I2C_SLAVE, m_slaveaddress) < 0) {
        cerr<<"C_I2C: Erro ao definir slave (ioctl)\n";
        return false;
    }

    return true;
}


void C_I2C::closeI2C() {
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}


bool C_I2C::writeRegister(uint8_t reg, uint8_t value) {

    uint8_t buffer[2];
    buffer[0] = reg;
    buffer[1] = value;

    if (write(m_fd, buffer, 2) != 2) {
        cerr<<"C_I2C: Erro ao escrever no registo\n";
        return false;
    }
    return true;
}

bool C_I2C::readRegister(uint8_t reg, uint8_t& value) {
    if (write(m_fd, &reg, 1) != 1) {
        cerr<<"C_I2C: Erro ao definir o registo\n";
        return false;
    }
    if (read(m_fd, &value, 1) != 1) {
        cerr<<"C_I2C: Erro ao ler do registo definido\n";
        return false;
    }
    return true;
}


bool C_I2C::readBytes(uint8_t reg, uint8_t* buffer, size_t len) {

    if (write(m_fd, &reg, 1) != 1) {
        cerr<<"C_I2C: Erro ao definir registo inicial\n";
        return false;
    }
    
    if (read(m_fd, buffer, len) != static_cast<ssize_t>(len)) {
        cerr<<"C_I2C: Erro na leitura do bloco\n";
        return false;
    }

    return true;
}

ssize_t C_I2C::readRaw(uint8_t* buffer, size_t len) {
    return read(m_fd, buffer, len);
}
