#include "ConnectionPool.h"
#include "fileUtils.h"
#include "sqlite3.h"
#include "sqliteBridge.h"
#include "sqliteExecute.h"

ConnectionPool::ConnectionPool(std::string dbName, std::string docPath,
                               unsigned int numReadConnections)
    : dbName(dbName), maxReads(numReadConnections),
      writeConnection(dbName, docPath,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX),
      commitPayload(
          {.dbName = &this->dbName, .event = TransactionEvent::COMMIT}),
      rollbackPayload({
          .dbName = &this->dbName,
          .event = TransactionEvent::ROLLBACK,
      }) {

  onContextCallback = nullptr;
  isConcurrencyEnabled = maxReads > 0;

  readConnections = new ConnectionState *[maxReads];
  // Open the read connections
  for (int i = 0; i < maxReads; i++) {
    readConnections[i] = new ConnectionState(
        dbName, docPath, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX);
  }
};

ConnectionPool::~ConnectionPool() {
  for (int i = 0; i < maxReads; i++) {
    delete readConnections[i];
  }
  delete readConnections;
}

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
      if (readConnections[i]->isEmptyLock()) {
        // There is an open slot
        activateContext(*readConnections[i], contextId);
        return;
      }
    }

    // If we made it here, there were no open slots, need to queue
    readQueue.push_back(contextId);
  }
}

void ConnectionPool::writeLock(ConnectionLockId contextId) {
  // Check if there are any available read connections
  if (writeConnection.isEmptyLock()) {
    activateContext(writeConnection, contextId);
    return;
  }

  // If we made it here, there were no open slots, need to queue
  writeQueue.push_back(contextId);
}

SQLiteOPResult
ConnectionPool::queueInContext(ConnectionLockId contextId,
                               std::function<void(sqlite3 *)> task) {
  ConnectionState *state = nullptr;
  if (writeConnection.matchesLock(contextId)) {
    state = &writeConnection;
  } else {
    // Check if it's a read connection
    for (int i = 0; i < maxReads; i++) {
      if (readConnections[i]->matchesLock(contextId)) {
        state = readConnections[i];
        break;
      }
    }
  }
  if (state == nullptr) {
    // return error that context is not available
    return SQLiteOPResult{
        .errorMessage = "Context is no longer available",
        .type = SQLiteError,
    };
  }

  state->queueWork(task);

  return SQLiteOPResult{
      .type = SQLiteOk,
  };
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

/**
 * The SQLite callback needs to return `0` in order for the commit to
 * proceed correctly
 */
int onCommitIntermediate(ConnectionPool *pool) {
  if (pool->onCommitCallback != NULL) {
    pool->onCommitCallback(&(pool->commitPayload));
  }
  return 0;
}

void ConnectionPool::setTransactionFinalizerHandler(
    void (*callback)(const TransactionCallbackPayload *)) {
  this->onCommitCallback = callback;
  sqlite3_commit_hook(writeConnection.connection,
                      (int (*)(void *))onCommitIntermediate, (void *)this);
  sqlite3_rollback_hook(writeConnection.connection, (void (*)(void *))callback,
                        (void *)&rollbackPayload);
}

void ConnectionPool::closeContext(ConnectionLockId contextId) {
  if (writeConnection.matchesLock(contextId)) {
    if (writeQueue.size() > 0) {
      // There are items in the queue, activate the next one
      activateContext(writeConnection, writeQueue[0]);
      writeQueue.erase(writeQueue.begin());
    } else {
      // No items in the queue, clear the context
      writeConnection.clearLock();
    }
  } else {
    // Check if it's a read connection
    for (int i = 0; i < maxReads; i++) {
      if (readConnections[i]->matchesLock(contextId)) {
        if (readQueue.size() > 0) {
          // There are items in the queue, activate the next one
          activateContext(*readConnections[i], readQueue[0]);
          readQueue.erase(readQueue.begin());
        } else {
          // No items in the queue, clear the context
          readConnections[i]->clearLock();
        }
        return;
      }
    }
  }
}

void ConnectionPool::closeAll() {
  writeConnection.close();
  for (int i = 0; i < maxReads; i++) {
    readConnections[i]->close();
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
    if (!connectionState->isEmptyLock()) {
      return SQLiteOPResult{
          .type = SQLiteError,
          .errorMessage = dbName + " was unable to attach another database: " +
                          "Some DB connections were locked",
      };
    }
  }

  for (auto &connectionState : dbConnections) {
    SequelLiteralUpdateResult result =
        sqliteExecuteLiteralWithDB(connectionState->connection, statement);
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
    if (!connectionState->isEmptyLock()) {
      return SQLiteOPResult{
          .type = SQLiteError,
          .errorMessage = dbName + " was unable to detach another database: " +
                          "Some DB connections were locked",
      };
    }
  }

  for (auto &connectionState : dbConnections) {
    SequelLiteralUpdateResult result =
        sqliteExecuteLiteralWithDB(connectionState->connection, statement);
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

// ===================== Private ===============

std::vector<ConnectionState *> ConnectionPool::getAllConnections() {
  std::vector<ConnectionState *> result;
  result.push_back(&writeConnection);
  for (int i = 0; i < maxReads; i++) {
    result.push_back(readConnections[i]);
  }
  return result;
}

void ConnectionPool::activateContext(ConnectionState &state,
                                     ConnectionLockId contextId) {
  state.activateLock(contextId);

  if (onContextCallback != nullptr) {
    onContextCallback(dbName, contextId);
  }
}
