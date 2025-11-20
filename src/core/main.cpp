#include <iostream>
#include <unistd.h>     // sleep()
#include "C_PWM.h"

using namespace std;

int main()
{
    cout << "Teste PWM a iniciar..." << endl;

    // pwmchip0, canal 0  → MUITO comum no Raspberry Pi
    C_PWM pwm(0, 0);
    C_PWM pwm2(0, 1);

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
    if (!pwm.setDutyCycle(50))    // 128 ≈ metade de 255
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

    if (!pwm2.init())
    {
        cerr << "Falha no init()" << endl;
        return 1;
    }
    cout << "PWM exportado com sucesso!" << endl;

    // 2. Definir o período (20ms → 20 000 000 ns)
    if (!pwm2.setPeriodns(20000000))
    {
        cerr << "Falha em setPeriod()" << endl;
        return 1;
    }
    cout << "Periodo configurado!" << endl;

    // 3. Meter duty a 50% (só para teste)
    if (!pwm2.setDutyCycle(50))    // 128 ≈ metade de 255
    {
        cerr << "Falha em setDutyCycle()" << endl;
        return 1;
    }
    cout << "Duty cycle configurado!" << endl;

    // 4. LIGAR PWM
    if (!pwm2.setEnable(true))
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

    return 0;
}