#include "C_I2C.h"
#include <iostream>
#include <fcntl.h>      // Para open()
#include <unistd.h>     // Para read(), write(), close()
#include <sys/ioctl.h>  // Para ioctl()
#include <linux/i2c-dev.h> // Constantes I2C (I2C_SLAVE)

using namespace std;

C_I2C::C_I2C(int bus, uint8_t address)
    : m_bus(bus), m_address(address), m_fd(-1)
{
    // Monta a string do caminho: "/dev/i2c-1"
    m_devicePath = "/dev/i2c-" + to_string(m_bus);
}

// --- Destrutor ---
C_I2C::~C_I2C() {
    closeI2C();
}

// --- INIT (Abrir e Configurar) ---
bool C_I2C::init() {
    // 1. OPEN (System Call)
    // Abre o ficheiro do dispositivo I2C
    m_fd = open(m_devicePath.c_str(), O_RDWR);
    if (m_fd < 0) {
        perror("C_I2C: Erro ao abrir o barramento");
        return false;
    }

    // 2. IOCTL (System Call)
    // Configura o Driver: "Tudo o que eu escrever agora vai para o endereço m_address q é o endereço do slave"
    // nao esquecer o q o ioctl ja tem documentaçao para cada shi e se pesquisarmos ele ja tem parametros q ja vem definifos para identificar o q temos de fazer
    // ISTO SRRVE PARA INDICAR O ENDEREÇO PARA ONDE O MASTER Q SERA A RASP vai falar
    if (ioctl(m_fd, I2C_SLAVE, m_address) < 0) {
        perror("C_I2C: Erro ao definir endereco escravo (ioctl)");
        return false;
    }

    return true;
}

// --- Fechar ---
void C_I2C::closeI2C() {
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

// para escrever algo q o sensor necessite
//provavelmente config do sensor em si
bool C_I2C::writeRegister(uint8_t reg, uint8_t value) {
    // Cria um pacote de 2 bytes: [Endereço da Gaveta] [Valor]
    uint8_t buffer[2];
    buffer[0] = reg;
    buffer[1] = value;

    // WRITE (System Call)
    // Envia os 2 bytes pelo fio. O sensor recebe e guarda o valor.
    if (write(m_fd, buffer, 2) != 2) {
        perror("C_I2C: Erro ao escrever no registo");
        return false;
    }
    return true;
}

// --- READ REGISTER (Ler um Dado) ---
uint8_t C_I2C::readRegister(uint8_t reg) {
    // Passo 1: Apontar o dedo (Write)
    // Enviamos apenas o endereço da gaveta para o sensor saber o que queremos
    if (write(m_fd, &reg, 1) != 1) {
        perror("C_I2C: Erro ao pedir registo (Write falhou)");
        return 0;
    }

    // Passo 2: Ouvir a resposta (Read)
    // Lemos 1 byte. O sensor devolve o conteúdo da gaveta apontada.
    uint8_t valor = 0;
    if (read(m_fd, &valor, 1) != 1) {
        perror("C_I2C: Erro ao ler valor (Read falhou)");
        return 0;
    }

    return valor;
}

// --- READ BYTES (Ler Bloco) ---
bool C_I2C::readBytes(uint8_t reg, uint8_t* buffer, size_t len) {
    // Passo 1: Apontar para o registo inicial
    if (write(m_fd, &reg, 1) != 1) {
        perror("C_I2C: Erro ao definir registo inicial");
        return false;
    }

    // Passo 2: Ler 'len' bytes de seguida
    // O sensor usa auto-incremento para nos dar as gavetas seguintes-> o sensor proprio sensor quando fazemos a leitura de mais de um vai autoincremnatar o registo
    //util para tipo quando tem por exemplo no regista 1 a parte alta e no 2 a parte baixa

    if (read(m_fd, buffer, len) != (ssize_t)len) {
        perror("C_I2C: Erro na leitura do bloco");
        return false;
    }

    return true;
}