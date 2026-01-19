#include "C_tInventoryScan.h"
#include <iostream>
#include <cstring>
#include <ctime>

C_tInventoryScan::C_tInventoryScan(C_Monitor& m_monitorservovault, C_YRM1001& m_rfidInventoy, C_Mqueue& m_mqToDatabase)
    : C_Thread(PRIO_LOW), m_monitorservovault(m_monitorservovault),
      m_rfidInventoy(m_rfidInventoy),
      m_mqToDatabase(m_mqToDatabase)
{
}

void C_tInventoryScan::run() {
    std::cout << "[InventoryScan] Thread iniciada. Monitorizando cofre..." << std::endl;

    while (!stopRequested()) {
        if (m_monitorservovault.timedWait(1)) {
            continue;
        }

        SensorData data = {};

        // 1. Leitura do inventário (Preenche o array na struct)
        if (m_rfidInventoy.read(&data)) {

            // 2. Preparar mensagem para a Base de Dados
            DatabaseMsg msg = {};
            msg.command = DB_CMD_UPDATE_ASSET;

            // Copiamos os dados do inventário para o payload da DB
            msg.payload.rfidInventory.tagCount = data.data.rfid_inventory.tagCount;

            for(int i = 0; i < data.data.rfid_inventory.tagCount; ++i) {
                strncpy(msg.payload.rfidInventory.tagList[i],
                        data.data.rfid_inventory.tagList[i], 32);
            }

            // 3. Enviar a lista completa para processamento
            m_mqToDatabase.send(&msg, sizeof(DatabaseMsg));

            // 4. Gerar log informativo do scan
            sendLog(data.data.rfid_inventory.tagCount);
        }
    }
}

void C_tInventoryScan::generateDescription(int count, char* buffer, size_t size) {
    // Descrição específica como pediste
    snprintf(buffer, size, "LEITURA INVENTÁRIO: %d itens confirmados após fecho", count);
}

void C_tInventoryScan::sendLog(int count) {
    DatabaseMsg logMsg = {};
    logMsg.command = DB_CMD_WRITE_LOG;

    logMsg.payload.log.logType = LOG_TYPE_INVENTORY; // Nome específico
    logMsg.payload.log.entityID = 0;
    logMsg.payload.log.value = (uint16_t)count;
    logMsg.payload.log.timestamp = (uint32_t)time(nullptr);

    generateDescription(count, logMsg.payload.log.description, sizeof(logMsg.payload.log.description));

    m_mqToDatabase.send(&logMsg, sizeof(DatabaseMsg));
}