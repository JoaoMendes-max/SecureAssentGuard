#ifndef C_GPIO_H
#define C_GPIO_H

#include <string>

// Enum para facilitar a leitura no main (ex: C_GPIO::OUT)
enum GPIO_DIRECTION { IN, OUT };

class C_GPIO {
public:
    // Construtor: Recebe o número do pino (ex: 17) e a direção
    C_GPIO(int pin, GPIO_DIRECTION dir);

    // Destrutor: Garante que o pino é libertado quando a classe morre
    ~C_GPIO();

    // --- FUNÇÕES DE CONTROLO ---

    // 1. Inicializar (Faz o 'export' e define o 'direction')
    // Retorna true se correu tudo bem.
    bool init();

    // 2. Fechar (Faz o 'unexport')
    // Liberta o pino para o sistema.
    void closePin();

    // 3. Escrever (Acender/Apagar)
    // value: true para 1 (High), false para 0 (Low)
    void writePin(bool value);

    // 4. Ler (Ver estado)
    // Retorna true se o pino estiver a 1, false se estiver a 0
    bool readPin();

private:
    int m_pin;              // Número do pino (ex: 17)
    GPIO_DIRECTION m_dir;   // Direção guardada (IN ou OUT)
    std::string m_path;     // Caminho base: "/sys/class/gpio/gpio17"
};

#endif // C_GPIO_H