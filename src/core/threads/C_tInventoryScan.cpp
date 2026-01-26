/*
 * Flow: wake on vault close -> scan RFID tags -> update DB + log.
 */

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
        // Wait for vault reed switch event.
        if (m_monitorservovault.timedWait(1)) {
            continue;
        }
        std::cout << "[InventoryScan] pia.." << std::endl;

        SensorData data = {};

        // Read inventory (tag list) from the YRM1001 reader.
        if (m_rfidInventoy.read(&data)) {
            DatabaseMsg msg = {};
            msg.command = DB_CMD_UPDATE_ASSET;
            msg.payload.rfidInventory.tagCount = data.data.rfid_inventory.tagCount;

            for(int i = 0; i < data.data.rfid_inventory.tagCount; ++i) {
                strncpy(msg.payload.rfidInventory.tagList[i],
                        data.data.rfid_inventory.tagList[i], 31);
                msg.payload.rfidInventory.tagList[i][31] = '\0';
            }
            m_mqToDatabase.send(&msg, sizeof(DatabaseMsg));
            sendLog(data.data.rfid_inventory.tagCount);
        }
    }
}

void C_tInventoryScan::generateDescription(int count, char* buffer, size_t size) {
    // Audit message for inventory.
    snprintf(buffer, size, "LEITURA INVENTÁRIO: %d itens confirmados após fecho", count);
}

void C_tInventoryScan::sendLog(int count) {
    DatabaseMsg logMsg = {};
    logMsg.command = DB_CMD_WRITE_LOG;

    logMsg.payload.log.logType = LOG_TYPE_INVENTORY; 
    logMsg.payload.log.entityID = 0;
    logMsg.payload.log.value = static_cast<double>(count);
    logMsg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(count, logMsg.payload.log.description, sizeof(logMsg.payload.log.description));

    m_mqToDatabase.send(&logMsg, sizeof(DatabaseMsg));
}
