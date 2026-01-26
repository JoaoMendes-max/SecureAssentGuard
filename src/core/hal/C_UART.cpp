/*
 * UART implementation based on termios.
 */

#include "C_UART.h"
#include <iostream>
#include <fcntl.h>      
#include <termios.h>    
#include <unistd.h>     
#include <cstring>      
#include <cerrno> 

using namespace std;

C_UART::C_UART(int portnumber)
    : m_fd(-1)
{
    // Map number to /dev/ttyAMA{n}.
    m_portPath = "/dev/ttyAMA" + to_string(portnumber);
}

C_UART::~C_UART() {
    closePort();
}

bool C_UART::openPort() {
    // Open the port in non-blocking mode.
    m_fd = open(m_portPath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd == -1) {
        perror("C_UART: Erro ao abrir porta");
        return false;
    }
    
    return true;
}



void C_UART::closePort() {
    if (m_fd != -1) {
        close(m_fd);
        m_fd = -1;
    }
}


bool C_UART::configPort(int baud, int bits, char parity) {
    if (m_fd == -1) return false;

    struct termios options = {0};

    // Load current configuration.
    if (tcgetattr(m_fd, &options) != 0) {
        perror("C_UART: Erro no tcgetattr");
        return false;
    }

    speed_t speed;
    // Map baud rate to termios constants.
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
    
    // Configure data bits.
    options.c_cflag &= ~CSIZE;
    if (bits == 8) {
        options.c_cflag |= CS8;
    } else if (bits == 7) {
        options.c_cflag |= CS7;
    } else {
        cerr << "C_UART: Bits deve ser 7 ou 8" << endl;
        return false;
    }
    // Configure parity.
    if (parity == 'N') {
        options.c_cflag &= ~PARENB; 
    } else if (parity == 'E') {
        options.c_cflag |= PARENB;  
        options.c_cflag &= ~PARODD; 
    } else if (parity == 'O') {
        options.c_cflag |= PARENB;  
        options.c_cflag |= PARODD;  
    } else {
        cerr << "C_UART: Paridade inválida (use 'N', 'E', 'O')" << endl;
        return false;
    }

    options.c_cflag &= ~CSTOPB;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_cflag &= ~CRTSCTS;
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;

    // Apply configuration immediately.
    if (tcsetattr(m_fd, TCSANOW, &options) != 0) {
        cerr<<"C_UART: Erro no tcsetattr\n";
        return false;
    }

    tcflush(m_fd, TCIOFLUSH);

    return true;
}


int C_UART::writeBuffer(const void* data, size_t len) {
    if (m_fd == -1) return -1;

    // Write bytes to the port.
    int count = write(m_fd, data, len);
    if (count < 0) cerr<<"C_UART: Erro ao escrever";

    return count;
}

int C_UART::readBuffer(void* buffer, size_t len) {
    if (m_fd == -1) return -1;

    // Non-blocking read.
    int count = read(m_fd, buffer, len);

    if (count < 0) {
        // No data available.
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        // Real I/O error.
        perror("C_UART: Erro real no read");
        return -1;
    }

    return count;
}
