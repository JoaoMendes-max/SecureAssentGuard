#include <iostream>
#include <unistd.h>     // sleep()
#include "C_PWM.h"
#include "C_UART.h"
#include <cstring>    // Para strlen, strcmp

using namespace std;

int main()
{
    /*
     *
     * lil nigga
     */

{ /*
    cout << "Teste PWM a iniciar..." << endl;

    // pwmchip0, canal 0  → MUITO comum no Raspberry Pi
    C_PWM pwm(0, 0);

    // 1. Exportar o PWM
    if (!pwm.init())
    {
        cerr << "Falha no init()" << endl;
        return 1;
    }
    cout << "PWM exportado com sucesso!" << endl;

    // 2. Definir o período (20ms → 20 000 000 ns)
    if (!pwm.setPeriodns(20000000))
    {
        cerr << "Falha em setPeriod()" << endl;
        return 1;
    }
    cout << "Periodo configurado!" << endl;

    // 3. Meter duty a 50% (só para teste)
    if (!pwm.setDutyCycle(50))
    {
        cerr << "Falha em setDutyCycle()" << endl;
        return 1;
    }
    cout << "Duty cycle configurado!" << endl;

    // 4. LIGAR PWM
    if (!pwm.setEnable(true))
    {
        cerr << "Falha em setEnable(true)" << endl;
        return 1;
    }
    cout << "PWM ligado!" << endl;

    // Mantém ligado 5 segundos
    while (true) {
        sleep(5);
    }
    // 5. DESLIGAR PWM
    pwm.setEnable(false);
    cout << "PWM desligado!" << endl;

    cout << "Teste concluído." << endl;

    return 0;*/
    // 1. Configurar UART 2 (Pinos 27 e 28)
    cout << "--- Iniciando Modo Terminal (UART 2) ---" << endl;
    cout << "Liga o FTDI e abre o CuteCom no PC (115200 baud)." << endl;

    C_UART uart(2);

    if (!uart.openPort()) {
        cerr << "ERRO: Nao consegui abrir a porta!" << endl;
        return -1;
    }

    if (!uart.configPort(115200, 8, 'N')) {
        cerr << "ERRO: Nao consegui configurar!" << endl;
        return -1;
    }

    // 2. Enviar uma mensagem de boas-vindas para o PC
    const char* msgHello = "\r\nOla PC! Eu sou a Raspberry Pi.\r\nEscreve algo...\r\n";
    uart.writeBuffer(msgHello, strlen(msgHello));

    // 3. O CICLO INFINITO (Ouvir e Responder)
    char buffer[100];

    while (true) {
        // Tenta ler o que tu escreveste no PC
        int n = uart.readBuffer(buffer, 99);

        if (n > 0) {
            buffer[n] = '\0'; // Terminar a string

            // Mostra no terminal da Raspberry (SSH)
            cout << "Recebi do PC: " << buffer << endl;

            // --- O "ECHO" (Mandar de volta) ---
            // Envia de volta para aparecer no teu CuteCom
            // Assim sabes que a comunicacao funciona nos dois sentidos
            uart.writeBuffer("Tu disseste: ", 13);
            uart.writeBuffer(buffer, n);
            uart.writeBuffer("\r\n", 2); // Nova linha
        }

        // Envia um "ping" a cada 5 segundos só para saberes que a Rasp está viva
        // (Podes comentar isto se for chato)
        static int contador = 0;
        contador++;
        if (contador % 500 == 0) { // 500 * 10ms = 5 segundos
            const char* ping = ".";
            uart.writeBuffer(ping, 1);
        }

        // Pausa pequena para não bloquear o CPU (10ms)
        usleep(10000);
    }

    uart.closePort();
    return 0;
}
