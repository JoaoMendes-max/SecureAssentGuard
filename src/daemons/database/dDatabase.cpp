#include "dDatabase.h"

// O construtor apenas guarda o nome/caminho do ficheiro
dDatabase::dDatabase(const std::string& dbPath)
    : m_db(nullptr), m_dbPath(dbPath) {
}

// O destruidor garante que, se te esqueceres, a base de dados fecha ao sair
dDatabase::~dDatabase() {
    close();
}

bool dDatabase::open() {
    // Tenta abrir o ficheiro. Se não existir, o SQLite cria-o automaticamente.
    // .c_str() converte a string do C++ para o formato que o SQLite (em C) entende.
    int result = sqlite3_open(m_dbPath.c_str(), &m_db);

    if (result != SQLITE_OK) {
        std::cerr << "ERRO: Não foi possível abrir a base de dados: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    std::cout << "SUCESSO: Ligado ao ficheiro: " << m_dbPath << std::endl;
    return true;
}

void dDatabase::close() {
    if (m_db) {
        // Fecha a ligação e liberta a memória
        sqlite3_close(m_db);
        m_db = nullptr; // Importante para não tentarmos usar um ponteiro inválido
        std::cout << "Base de dados fechada corretamente." << std::endl;
    }
}