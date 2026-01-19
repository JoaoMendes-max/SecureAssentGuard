// C_tTestWorker.h
#include "C_Thread.h"
#include "C_Monitor.h"
#include "C_RDM6300.h"
#include  "C_YRM1001.h"

class C_tTestWorker : public C_Thread {
private:
    C_Monitor& m_mon;
    C_RDM6300 rfident;

    std::string m_nome;

public:
    C_tTestWorker(C_Monitor& mon, C_RDM6300& rfident, std::string nome) : C_Thread(PRIO_MEDIUM),m_mon(mon), m_nome(nome), rfident(rfident) {}

   void run() override {
        std::cout << "[Teste] Thread " << m_nome << " à espera..." << std::endl;
        SensorData data={};// confitma esta merda de criar locais !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        while (true) {

            m_mon.wait(); // Ela fica aqui a dormir até o Sighandler fazer notify

            if (rfident.read(&data)) {

                std::cout << "ID: " << data.data.rfid_single.tagID << std::endl;
            }
            //std::cout << ">>> SUCESSO: A thread " << m_nome << " foi acordada pelo sinal! <<<" << std::endl;

        }
    }
};






