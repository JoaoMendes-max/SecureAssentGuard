#include "C_GPIO.h"
#include <cstdio>       // sprintf
#include <fcntl.h>      // open
#include <unistd.h>     // write, read, close
#include <cstring>      // strlen
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

// --- INIT ---
bool C_GPIO::init() {
    // 1. EXPORTAR (Usando write)
    // Temos de abrir o ficheiro "export" principal
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd == -1) {
        perror("GPIO: Erro ao abrir export");
        return false;
    }

    string pinStr = to_string(m_pin);
    write(fd, pinStr.c_str(), pinStr.length()); // Escreve "17"
    close(fd); // Fecha logo

    // Esperar que o Linux crie a pasta (Crítico!)
    //usleep(100000);

    // 2. DIREÇÃO
    string dirPath = m_path + "/direction";
    fd = open(dirPath.c_str(), O_WRONLY);
    if (fd == -1) {
        perror("GPIO: Erro ao abrir direction");
        return false;
    }

    const char* d = (m_dir == OUT) ? "out" : "in";
    write(fd, d, strlen(d)); // Escreve "out" ou "in"
    close(fd);

    return true;
}

// --- CLOSE (Unexport) ---
void C_GPIO::closePin() {
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd != -1) {
        string pinStr = to_string(m_pin);
        write(fd, pinStr.c_str(), pinStr.length());
        close(fd);
    }
}

// --- WRITE (Acender/Apagar) ---
void C_GPIO::writePin(bool value) {
    if (m_dir != OUT) return;

    string valPath = m_path + "/value";

    // 1. Abrir
    int fd = open(valPath.c_str(), O_WRONLY);
    if (fd == -1) return;

    // 2. Escrever ("1" ou "0")
    if (value) {
        write(fd, "1", 1);
    } else {
        write(fd, "0", 1);
    }

    // 3. Fechar
    close(fd);
}

// --- READ (Ler Estado) ---
bool C_GPIO::readPin() {
    string valPath = m_path + "/value";

    int fd = open(valPath.c_str(), O_RDONLY);
    if (fd == -1) return false;

    char buffer[1] = {0};
    read(fd, buffer, 1); // Lê 1 byte ("0" ou "1")
    close(fd);

    return (buffer[0] == '1');
}