#include "C_UART.h"
#include <iostream>
#include <fcntl.h>      // File Control Definitions
#include <termios.h>    // POSIX Terminal Control Definitions
#include <unistd.h>     // UNIX Standard Definitions
#include <cstring>      // memset
#include <cerrno> // Necessário para usar 'errno'

using namespace std;

C_UART::C_UART(int portnumber)
    : m_fd(-1)
{
    m_portPath = "/dev/ttyAMA" + to_string(portnumber);
}

C_UART::~C_UART() {
    closePort();
}

bool C_UART::openPort() {
    // Adiciona O_NONBLOCK explicitamente
    m_fd = open(m_portPath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (m_fd == -1) {
        perror("C_UART: Erro ao abrir porta");
        return false;
    }
    // IMPORTANTE: Remove qualquer fcntl que tinhas aqui em baixo
    return true;
}


/*

bool C_UART::openPort() {
    //logic to ignore dcd(data carrier detect)
    m_fd = open(m_portPath.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);

    if (m_fd == -1) {
        cerr<<("C_UART: Erro ao abrir porta\n");
        return false;
    }

    fcntl(m_fd, F_SETFL, 0);

    return true;
}
*/

void C_UART::closePort() {
    if (m_fd != -1) {
        close(m_fd);
        m_fd = -1;
    }
}


bool C_UART::configPort(int baud, int bits, char parity) {
    if (m_fd == -1) return false;

    struct termios options = {0};

    // 1.Will get the present configurations
    if (tcgetattr(m_fd, &options) != 0) {
        perror("C_UART: Erro no tcgetattr");
        return false;
    }

    // 2. configure baudrate
    speed_t speed;
    switch (baud) {
        case 9600:   speed = B9600; break;
        case 19200:  speed = B19200; break;
        case 38400:  speed = B38400; break;
        case 57600:  speed = B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
        default:
            cerr << "C_UART: Baudrate não suportado!" << endl;
            return false;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    // 3. Configure data bytes
    options.c_cflag &= ~CSIZE;
    if (bits == 8) {
        options.c_cflag |= CS8;
    } else if (bits == 7) {
        options.c_cflag |= CS7;
    } else {
        cerr << "C_UART: Bits deve ser 7 ou 8" << endl;
        return false;
    }

    // 4. Configure PArity
    if (parity == 'N') {
        options.c_cflag &= ~PARENB; // Desliga bit de paridade
    } else if (parity == 'E') {
        options.c_cflag |= PARENB;  // Liga paridade
        options.c_cflag &= ~PARODD; // É par (não ímpar)
    } else if (parity == 'O') {
        options.c_cflag |= PARENB;  // Liga paridade
        options.c_cflag |= PARODD;  // É ímpar
    } else {
        cerr << "C_UART: Paridade inválida (use 'N', 'E', 'O')" << endl;
        return false;
    }

    // 5. Configurar Stop Bits (Sempre 1 por padrão)
    options.c_cflag &= ~CSTOPB;

    // 6.
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;
    options.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Hardware Flow Control (Disable RTS/CTS because we only use tx and rx)
    options.c_cflag &= ~CRTSCTS;

    // Disable modem flags like the neeed of DCD
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;

    // 8.Configurations,
    if (tcsetattr(m_fd, TCSANOW, &options) != 0) {
        cerr<<"C_UART: Erro no tcsetattr\n";
        return false;
    }

    // 9. Limpar buffers antigos
    tcflush(m_fd, TCIOFLUSH);

    return true;
}


int C_UART::writeBuffer(const void* data, size_t len) {
    if (m_fd == -1) return -1;

    int count = write(m_fd, data, len);
    if (count < 0) cerr<<"C_UART: Erro ao escrever";

    return count;
}
/*
int C_UART::readBuffer(void* buffer, size_t len) {
    if (m_fd == -1) return -1;

    int count = read(m_fd, buffer, len);
    if (count < 0) cerr<<"C_UART: Erro ao ler";

    return count;
}
*/
int C_UART::readBuffer(void* buffer, size_t len) {
    if (m_fd == -1) return -1;

    int count = read(m_fd, buffer, len);

    if (count < 0) {
        // Se o erro for "Não há dados disponíveis" (EAGAIN), retorna 0
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        // Outros erros reais
        perror("C_UART: Erro real no read");
        return -1;
    }

    return count;
}
