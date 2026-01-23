#include "C_GPIO.h"
#include <cstdio>       
#include <fcntl.h>      
#include <unistd.h>     
#include <cstring>      
#include <iostream>
#define GPIO_BASE 512
using namespace std;

C_GPIO::C_GPIO(int pin, GPIO_DIRECTION dir)
    : m_dir(dir)
{
    m_pin=pin+GPIO_BASE;
    m_path = "/sys/class/gpio/gpio" + to_string(m_pin);
}

C_GPIO::~C_GPIO() {
    closePin();
}

bool C_GPIO::init() {
    
    
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd == -1) {
        perror("GPIO: Erro ao abrir export");
        return false;
    }

    string pinStr = to_string(m_pin);
    write(fd, pinStr.c_str(), pinStr.length()); 
    close(fd);
    
    string dirPath = m_path + "/direction";
    fd = open(dirPath.c_str(), O_WRONLY);
    if (fd == -1) {
        perror("GPIO: Erro ao abrir direction");
        return false;
    }

    const char* d = (m_dir == OUT) ? "out" : "in";
    write(fd, d, strlen(d)); 
    close(fd);
    return true;
}


void C_GPIO::closePin() {
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd != -1) {
        string pinStr = to_string(m_pin);
        write(fd, pinStr.c_str(), pinStr.length());
        close(fd);
    }
}


void C_GPIO::writePin(bool value) {
    if (m_dir != OUT) return;
    string valPath = m_path + "/value";
    int fd = open(valPath.c_str(), O_WRONLY);
    if (fd == -1) return;
    
    if (value) {
        write(fd, "1", 1);
    } else {
        write(fd, "0", 1);
    }

    close(fd);
}

bool C_GPIO::readPin() {
    string valPath = m_path + "/value";

    int fd = open(valPath.c_str(), O_RDONLY);
    if (fd == -1) return false;

    char buffer[1] = {0};
    read(fd, buffer, 1); 
    close(fd);

    return (buffer[0] == '1');
}