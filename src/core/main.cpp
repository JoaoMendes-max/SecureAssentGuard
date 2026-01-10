#include <iostream>
#include <unistd.h>
#include "C_Fingerprint.h"
#include "C_UART.h"
#include "C_GPIO.h"
#include "C_Mqueue.h"
#include "C_RDM6300.h"
#include "C_tSighandler.h"
#include "C_YRM1001.h"

using namespace std;
/*
int main() {
    cout << "=== TESTE DE VERIFICACAO (MATCH) ===" << endl;

    // 1. Hardware Setup
    C_UART uart(2);
    C_GPIO rst(26, OUT);
    C_Fingerprint finger(uart, rst);

    if (!finger.init()) {
        cerr << "Erro no Init." << endl;
        return -1;
    }

    // 2. Ligar Sensor
    finger.wakeUp();
    cout << "Sensor ligado. A espera do dedo..." << endl;

    // 3. Loop de Verificação
    while (true) {
        cout << "\n>>> PODE POR O DEDO AGORA <<<" << endl;

        SensorData data;

        // Tenta ler com timeout de 5 segundos
        if (finger.read(&data)) {

            if (data.data.fingerprint.authenticated) {
                // SUCESSO
                cout << "#############################################" << endl;
                cout << "# [SUCESSO] ACESSO AUTORIZADO!              #" << endl;
                cout << "# User ID: " << data.data.fingerprint.userID << "                                #" << endl;
                cout << "#############################################" << endl;
            } else {
                // FALHA (Dedo errado ou mal colocado)
                cout << ">> [RECUSADO] Dedo nao reconhecido." << endl;
            }

            // Pausa para não ler o mesmo toque 50 vezes seguidas
            cout << "(Tira o dedo... espera 2s)" << endl;
            sleep(2);

        } else {
            // Timeout (Ninguém pôs o dedo em 5s)
            cout << "." << flush;
        }
    }

    return 0;
}*/

/*
 * cout << "--- DEBUG: ADICIONAR USER 1 ---" << endl;

    // 1. Hardware
    // NOTA: Confirma fisicamente se o fio RST está no GPIO 26.
    // (No teu Python tinhas o pino 24, no C++ tinhas 26. Verifica qual deles estás a usar).
    cout << "[1] A configurar hardware (UART2, GPIO26)..." << endl;

    C_UART uart(2);         // Usa /dev/ttyAMA2
    C_GPIO rst(26, OUT);    // Pino de Reset
    C_Fingerprint finger(uart, rst);

    // 2. Init Drivers
    cout << "[2] A inicializar drivers..." << endl;
    if (!finger.init()) {
        cerr << "ERRO: Init falhou (falha ao abrir UART ou exportar GPIO)." << endl;
        return -1;
    }

    // ---------------------------------------------------------
    // 3. HARD RESET MANUAL (A solução para o sensor "morto")
    // ---------------------------------------------------------
    cout << "[3] A executar Ciclo de Reset (Power Cycle)..." << endl;

    // Passo A: Forçar Desligar (RST = LOW)
    // Isto garante que o sensor reinicia o microcontrolador interno
    finger.sleep();
    cout << "   -> RST em LOW (Desligado)... a esperar 200ms" << endl;
    usleep(200000);       // 200ms (0.2s) - Tempo igual ao script Python

    // Passo B: Ligar (RST = HIGH)
    finger.wakeUp();
    cout << "   -> RST em HIGH (Ligado)... a esperar 300ms pelo Boot" << endl;

    // O sensor demora cerca de 200-300ms a arrancar o sistema operativo interno
    // antes de poder responder à UART. Sem isto, ele ignora os comandos.
    usleep(300000);       // 300ms (0.3s)

    cout << "   -> Sensor Reiniciado e Pronto." << endl;
    // ---------------------------------------------------------

    // 4. Add User
    cout << "\n[4] A chamar addUser(1)..." << endl;
    cout << ">>> TENS 10 SEGUNDOS PARA COLOCAR O DEDO EM CADA ETAPA <<<" << endl;

    // Se a comunicação estiver a funcionar, verás os logs do executeCommand agora
    bool result = finger.addUser(2);

    if (result) {
        cout << "\n[5] SUCESSO: Utilizador 1 adicionado!" << endl;
    } else {
        cout << "\n[5] FALHA: Não foi possível adicionar o utilizador." << endl;
    }

    // 5. Fim - Colocar a dormir para poupar energia
    cout << "[6] A desligar (Sleep)." << endl;
    finger.sleep();

    return 0;
 **/

/*
int main() {
    C_Mqueue mqToDb("mq_to_db",512,10,false);

    C_UART uart2(2);
    C_GPIO enable(26,OUT);

    C_RDM6300 rfident(uart2);
    //C_YRM1001 rfidinvent(uart2,enable);



    //rfidinvent.init();
    rfident.init();

    // 1. Bloqueio estático (Obrigatório)
    C_tSighandler::setupSignalBlock();

    // 2. Criar os monitores
    C_Monitor monReed, monPIR, monFinger, monRFID;

    // 3. Criar a nossa thread de sinais
    C_tSighandler sigHandler(monReed, monPIR, monFinger, monRFID);

    // 4. Criar a thread de teste ligada APENAS ao monitor do Reed Switch
    C_tTestWorker testWorker(monRFID,rfident, "REED_SWITCH_TEST");


    // 5. Arrancar as duas
    sigHandler.start();
    testWorker.start();

    std::cout << "[Main] Teste de sinal iniciado. À espera do hardware ou 'kill'..." << std::endl;

    while(true) pause();
    return 0;
}*/


#include <cstring>


using namespace std;

int main() {
    cout << "[SIMULADOR] A iniciar teste de envio para a DB..." << endl;

    // 1. Ligar à fila que a DB já criou
    // Nome: "mq_to_db", Tamanho: 512, Max: 10, createNew: false
    C_Mqueue m_mqToDatabase("/mq_to_db", sizeof(DatabaseMsg), 10, false);

    // 2. Preparar a mensagem (Simulando a Thread)
    DatabaseMsg msg = {};
    msg.command = DB_CMD_ENTER_ROOM_RFID;

    const char* rfidSimulado = "1234567890"; // Exemplo de um ID de cartão
    strncpy(msg.payload.rfid, rfidSimulado, sizeof(msg.payload.rfid) - 1);
    msg.payload.rfid[sizeof(msg.payload.rfid) - 1] = '\0'; // Garantir o fim da string
    cout << sizeof(DatabaseMsg) << endl;
    cout << "[SIMULADOR] A enviar RFID: " << msg.payload.rfid << " para a fila mq_to_db..." << endl;

    // 3. Enviar
    if (m_mqToDatabase.send(&msg, sizeof(msg))) {
        cout << "[SIMULADOR] Mensagem enviada com sucesso!" << endl;
    } else {
        cerr << "[ERRO] Falha ao enviar a mensagem. A BD está a correr?" << endl;
    }

    cout << "[SIMULADOR] Teste terminado." << endl;
    return 0;
}