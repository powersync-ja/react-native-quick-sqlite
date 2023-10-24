#include "ConnectionPool.h"
#include "fileUtils.h"
#include "sqlite3.h"
#include "sqliteBridge.h"
#include "sqliteExecute.h"
#include <fstream>
#include <iostream>

ConnectionPool::ConnectionPool(std::string dbName, std::string docPath,
                               unsigned int numReadConnections)
    : dbName(dbName), maxReads(numReadConnections) {

  onContextCallback = nullptr;
  isConcurrencyEnabled = maxReads > 0;

  struct ConnectionState writeCon;
  writeCon.connection = nullptr;
  writeCon.currentLockId = EMPTY_LOCK_ID;

  writeConnection = writeCon;

  // Open the write connection
  genericSqliteOpenDb(dbName, docPath, &writeConnection.connection,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX);

  // Open the read connections
  for (int i = 0; i < maxReads; i++) {
    sqlite3 *db;
    genericSqliteOpenDb(dbName, docPath, &db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX);
    struct ConnectionState readCon;
    readCon.connection = db;
    readCon.currentLockId = EMPTY_LOCK_ID;
    readConnections.push_back(readCon);
  }

  if (true == isConcurrencyEnabled) {
    // Write connection
    sqliteExecuteLiteralWithDB(this->writeConnection.connection,
                               "PRAGMA journal_mode = WAL;");
    sqliteExecuteLiteralWithDB(
        this->writeConnection.connection,
        "PRAGMA journal_size_limit = 6291456"); // 6Mb 1.5x default checkpoint
                                                // size
    // Default to normal on all connections
    sqliteExecuteLiteralWithDB(this->writeConnection.connection,
                               "PRAGMA synchronous = NORMAL;");

    // Read connections
    for (int i = 0; i < this->maxReads; i++) {
      sqliteExecuteLiteralWithDB(this->readConnections[i].connection,
                                 "PRAGMA synchronous = NORMAL;");
    }
  }
};

ConnectionPool::~ConnectionPool() {}

void ConnectionPool::readLock(ConnectionLockId contextId) {
  // Maintain compatibility if no concurrent read connections are present
  if (false == isConcurrencyEnabled) {
    return writeLock(contextId);
  }

  // Check if there are any available read connections
  if (readQueue.size() > 0) {
    // There are already items queued
    readQueue.push_back(contextId);
  } else {
    // Check if there are open slots
    for (int i = 0; i < maxReads; i++) {
      if (readConnections[i].currentLockId == EMPTY_LOCK_ID) {
        // There is an open slot
        activateContext(&readConnections[i], contextId);
        return;
      }
    }

    // If we made it here, there were no open slots, need to queue
    readQueue.push_back(contextId);
  }
}

void ConnectionPool::writeLock(ConnectionLockId contextId) {
  // Check if there are any available read connections
  if (writeConnection.currentLockId == EMPTY_LOCK_ID) {
    activateContext(&writeConnection, contextId);
    return;
  }

  // If we made it here, there were no open slots, need to queue
  writeQueue.push_back(contextId);
}

SQLiteOPResult ConnectionPool::executeInContext(
    ConnectionLockId contextId, string const &query, vector<QuickValue> *params,
    vector<map<string, QuickValue>> *results,
    vector<QuickColumnMetadata> *metadata) {
  sqlite3 *db = nullptr;
  if (writeConnection.currentLockId == contextId) {
    db = writeConnection.connection;
  } else {
    // Check if it's a read connection
    for (int i = 0; i < maxReads; i++) {
      if (readConnections[i].currentLockId == contextId) {
        db = readConnections[i].connection;
        break;
      }
    }
  }
  if (db == nullptr) {
    // throw error that context is not available
    return SQLiteOPResult{
        .errorMessage = "Context is no longer available",
        .type = SQLiteError,
    };
  }

  return sqliteExecuteWithDB(db, query, params, results, metadata);
}

SequelLiteralUpdateResult
ConnectionPool::executeLiteralInContext(ConnectionLockId contextId,
                                        string const &query) {
  sqlite3 *db = nullptr;
  if (writeConnection.currentLockId == contextId) {
    db = writeConnection.connection;
  } else {
    // Check if it's a read connection
    for (int i = 0; i < maxReads; i++) {
      if (readConnections[i].currentLockId == contextId) {
        db = readConnections[i].connection;
        break;
      }
    }
  }
  if (db == nullptr) {
    // throw error that context is not available
    return SequelLiteralUpdateResult{
        .type = SQLiteError,
        .message = "Context is no longer available",
    };
  }

  return sqliteExecuteLiteralWithDB(db, query);
}

void ConnectionPool::setOnContextAvailable(void (*callback)(std::string,
                                                            ConnectionLockId)) {
  onContextCallback = callback;
}

void ConnectionPool::setTableUpdateHandler(
    void (*callback)(void *, int, const char *, const char *, sqlite3_int64)) {
  // Only the write connection can make changes
  sqlite3_update_hook(writeConnection.connection, callback,
                      (void *)(dbName.c_str()));
}

void ConnectionPool::closeContext(ConnectionLockId contextId) {
  if (writeConnection.currentLockId == contextId) {
    if (writeQueue.size() > 0) {
      // There are items in the queue, activate the next one
      activateContext(&writeConnection, writeQueue[0]);
      writeQueue.erase(writeQueue.begin());
    } else {
      // No items in the queue, clear the context
      writeConnection.currentLockId = EMPTY_LOCK_ID;
    }
  } else {
    // Check if it's a read connection
    for (int i = 0; i < maxReads; i++) {
      if (readConnections[i].currentLockId == contextId) {
        if (readQueue.size() > 0) {
          // There are items in the queue, activate the next one
          activateContext(&readConnections[i], readQueue[0]);
          readQueue.erase(readQueue.begin());
        } else {
          // No items in the queue, clear the context
          readConnections[i].currentLockId = EMPTY_LOCK_ID;
        }
        return;
      }
    }
  }
}

void ConnectionPool::closeAll() {
  sqlite3_close_v2(writeConnection.connection);
  for (int i = 0; i < maxReads; i++) {
    sqlite3_close_v2(readConnections[i].connection);
  }
}

SQLiteOPResult ConnectionPool::attachDatabase(std::string const dbFileName,
                                              std::string const docPath,
                                              std::string const alias) {

  /**
   * There is no need to check if mainDBName is opened because
   * sqliteExecuteLiteral will do that.
   * */
  string dbPath = get_db_path(dbFileName, docPath);
  string statement = "ATTACH DATABASE '" + dbPath + "' AS " + alias;

  auto dbConnections = getAllConnections();

  for (auto &connectionState : dbConnections) {
    if (connectionState.currentLockId != EMPTY_LOCK_ID) {
      return SQLiteOPResult{
          .type = SQLiteError,
          .errorMessage = dbName + " was unable to attach another database: " +
                          "Some DB connections were locked",
      };
    }
  }

  for (auto &connectionState : dbConnections) {
    SequelLiteralUpdateResult result =
        sqliteExecuteLiteralWithDB(connectionState.connection, statement);
    if (result.type == SQLiteError) {
      // Revert change on any successful connections
      detachDatabase(alias);
      return SQLiteOPResult{
          .type = SQLiteError,
          .errorMessage = dbName + " was unable to attach another database: " +
                          string(result.message),
      };
    }
  }

  return SQLiteOPResult{
      .type = SQLiteOk,
  };
}

SQLiteOPResult ConnectionPool::detachDatabase(std::string const alias) {
  /**
   * There is no need to check if mainDBName is opened because
   * sqliteExecuteLiteral will do that.
   * */
  string statement = "DETACH DATABASE " + alias;
  auto dbConnections = getAllConnections();

  for (auto &connectionState : dbConnections) {
    if (connectionState.currentLockId != EMPTY_LOCK_ID) {
      return SQLiteOPResult{
          .type = SQLiteError,
          .errorMessage = dbName + " was unable to detach another database: " +
                          "Some DB connections were locked",
      };
    }
  }

  for (auto &connectionState : dbConnections) {
    SequelLiteralUpdateResult result =
        sqliteExecuteLiteralWithDB(connectionState.connection, statement);
    if (result.type == SQLiteError) {
      return SQLiteOPResult{
          .type = SQLiteError,
          .errorMessage = dbName + " was unable to attach another database: " +
                          string(result.message),
      };
    }
  }
  return SQLiteOPResult{
      .type = SQLiteOk,
  };
}

SequelBatchOperationResult
ConnectionPool::importSQLFile(std::string fileLocation) {
  std::string line;
  std::ifstream sqFile(fileLocation);

  if (sqFile.is_open()) {
    sqlite3 *connection = writeConnection.connection;
    try {
      int affectedRows = 0;
      int commands = 0;
      sqliteExecuteLiteralWithDB(connection, "BEGIN EXCLUSIVE TRANSACTION");
      while (std::getline(sqFile, line, '\n')) {
        if (!line.empty()) {
          SequelLiteralUpdateResult result =
              sqliteExecuteLiteralWithDB(connection, line);
          if (result.type == SQLiteError) {
            sqliteExecuteLiteralWithDB(connection, "ROLLBACK");
            sqFile.close();
            return {SQLiteError, result.message, 0, commands};
          } else {
            affectedRows += result.affectedRows;
            commands++;
          }
        }
      }
      sqFile.close();
      sqliteExecuteLiteralWithDB(connection, "COMMIT");
      return {SQLiteOk, "", affectedRows, commands};
    } catch (...) {
      sqFile.close();
      sqliteExecuteLiteralWithDB(connection, "ROLLBACK");
      return {SQLiteError,
              "[react-native-quick-sqlite][loadSQLFile] Unexpected error, "
              "transaction was rolledback",
              0, 0};
    }
  } else {
    return {SQLiteError,
            "[react-native-quick-sqlite][loadSQLFile] Could not open file", 0,
            0};
  }
}

// ===================== Private ===============

std::vector<ConnectionState> ConnectionPool::getAllConnections() {
  std::vector<ConnectionState> result;
  result.push_back(writeConnection);
  for (int i = 0; i < maxReads; i++) {
    result.push_back(readConnections[i]);
  }
  return result;
}

void ConnectionPool::activateContext(ConnectionState *state,
                                     ConnectionLockId contextId) {
  state->currentLockId = contextId;

  if (onContextCallback != nullptr) {
    onContextCallback(dbName, contextId);
  }
}

SQLiteOPResult ConnectionPool::genericSqliteOpenDb(string const dbName,
                                                   string const docPath,
                                                   sqlite3 **db,
                                                   int sqlOpenFlags) {
  string dbPath = get_db_path(dbName, docPath);

  int exit = 0;
  exit = sqlite3_open_v2(dbPath.c_str(), db, sqlOpenFlags, nullptr);

  if (exit != SQLITE_OK) {
    return SQLiteOPResult{.type = SQLiteError,
                          .errorMessage = sqlite3_errmsg(*db)};
  }

  return SQLiteOPResult{.type = SQLiteOk, .rowsAffected = 0};
}
