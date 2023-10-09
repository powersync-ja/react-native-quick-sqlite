#include "ConnectionPool.h"
#include "sqlite3.h"

ConnectionPool::ConnectionPool(std::string dbName, std::string docPath,
                               unsigned int numReadConnections)
    : dbName(dbName), maxReads(numReadConnections) {

  lastLockId = EMPTY_LOCK_ID;
  onContextCallback = nullptr;

  struct ConnectionState writeCon;
  writeCon.connection = nullptr;
  writeCon.currentLockId = EMPTY_LOCK_ID;

  writeConnection = writeCon;

  // Open the write connection, TODO add correct flags
  genericSqliteOpenDb(dbName, docPath, &writeConnection.connection,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_NOMUTEX);

  // Open the read connections
  for (int i = 0; i < maxReads; i++) {
    sqlite3 *db;
    genericSqliteOpenDb(dbName, docPath, &db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX);
    // TODO setup pragmas for WAL
    struct ConnectionState readCon;
    readCon.connection = db;
    readCon.currentLockId = EMPTY_LOCK_ID;
    readConnections.push_back(readCon);
  }

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
};

ConnectionPool::~ConnectionPool() {}

void ConnectionPool::readLock(ConnectionLockId contextId) {
  // Create a new Id for a new lock
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

// ===================== Private ===============

void ConnectionPool::activateContext(ConnectionState *state,
                                     ConnectionLockId contextId) {
  state->currentLockId = contextId;

  if (onContextCallback != nullptr) {
    onContextCallback(dbName, contextId);
  }
}
