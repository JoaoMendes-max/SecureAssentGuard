#include "C_UART.h"
#include <iostream>
#include <fcntl.h>      // File Control Definitions
#include <termios.h>    // POSIX Terminal Control Definitions
#include <unistd.h>     // UNIX Standard Definitions
#include <cstring>      // memset

using namespace std;

// --- Construtor ---
C_UART::C_UART(int portnumber)
    : m_fd(-1)
{
    m_portPath = "/dev/ttyAMA" + std::to_string(portnumber);
}

// --- Destrutor ---
C_UART::~C_UART() {
    closePort();
}

// --- Abrir Porta ---
bool C_UART::openPort() {
    // O_RDWR: Leitura e Escrita
    // O_NOCTTY: Não deixa a porta controlar o terminal (importante em Linux)
    // O_NDELAY: Não bloqueia na abertura (útil se o cabo estiver desligado)
    m_fd = open(m_portPath.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);

    if (m_fd == -1) {
        perror("C_UART: Erro ao abrir porta"); // Imprime o erro exato do sistema
        return false;
    }

    // Limpa a flag O_NDELAY para que as leituras funcionem normalmente (bloqueiem conforme o timeout)
    fcntl(m_fd, F_SETFL, 0);

    return true;
}

<<<<<<< Updated upstream
// --- Fechar Porta --- fuck you mean fechar porta
=======
// --- Fechar Porta ---
>>>>>>> Stashed changes
void C_UART::closePort() {
    if (m_fd != -1) {
        close(m_fd);
        m_fd = -1;
    }
}

// --- Configurar (A parte complexa) ---
bool C_UART::configPort(int baud, int bits, char parity) {
    if (m_fd == -1) return false;

    struct termios options = {0};

    // 1. Obter a configuração atual para não estragar tudo
    if (tcgetattr(m_fd, &options) != 0) {
        perror("C_UART: Erro no tcgetattr");
        return false;
    }

    // 2. Configurar Baud Rate (Switch Case para traduzir int -> flag)
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

    // 3. Configurar Bits de Dados (7 ou 8)
    options.c_cflag &= ~CSIZE; // Limpa a máscara de tamanho
    if (bits == 8) {
        options.c_cflag |= CS8;
    } else if (bits == 7) {
        options.c_cflag |= CS7;
    } else {
        cerr << "C_UART: Bits deve ser 7 ou 8" << endl;
        return false;
    }

    // 4. Configurar Paridade (N, E, O)
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

    // 6. MODO RAW (Cru) - Resolve o problema da "escadinha" e caracteres estranhos
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Desliga modo canónico e eco
    options.c_oflag &= ~OPOST;                          // Desliga processamento de saída
    options.c_iflag &= ~(IXON | IXOFF | IXANY);         // Desliga controlo de fluxo software (XON/XOFF)

    // Hardware Flow Control (Desligar RTS/CTS para usar só 2 fios)
    options.c_cflag &= ~CRTSCTS;

    // Ativar recetor e ignorar linhas de controlo de modem
    options.c_cflag |= (CLOCAL | CREAD);

    // 7. Configurar Timeouts (Non-blocking com timeout)
    // VMIN = 0, VTIME = 10 -> Espera até 1 segundo (10 * 0.1s) por dados
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;

    // 8. Aplicar Configurações (O ioctl acontece aqui!)
    if (tcsetattr(m_fd, TCSANOW, &options) != 0) {
        perror("C_UART: Erro no tcsetattr");
        return false;
    }

    // 9. Limpar buffers antigos
    tcflush(m_fd, TCIOFLUSH);

    return true;
}

// --- Escrever Buffer ---
int C_UART::writeBuffer(const void* data, size_t len) {
    if (m_fd == -1) return -1;

    // write retorna o número de bytes escritos ou -1 em erro
    int count = write(m_fd, data, len);
    if (count < 0) perror("C_UART: Erro ao escrever");

    return count;
}

// --- Ler Buffer ---
int C_UART::readBuffer(void* buffer, size_t len) {
    if (m_fd == -1) return -1;

    // read retorna o número de bytes lidos ou -1 em erro (ou 0 se timeout)
    int count = read(m_fd, buffer, len);
    // Nota: Não imprimimos erro aqui porque timeout (0) é normal

    return count;
}

