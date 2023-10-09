#include "ConcurrentSQLiteBridge.h"
#include "ConnectionPool.h"
#include <map>

SQLiteOPResult generateNotOpenResult(std::string const &dbName) {
  return SQLiteOPResult{
      .type = SQLiteError,
      .errorMessage = dbName + " is not open",
  };
}

std::map<std::string, ConnectionPool *> concurrentDBMap =
    std::map<std::string, ConnectionPool *>();

SQLiteOPResult sqliteOpenDBConcurrent(
    string const dbName, string const docPath,
    void (*contextAvailableCallback)(std::string, ConnectionLockId),
    void (*updateTableCallback)(void *, int, const char *, const char *,
                                sqlite3_int64)) {
  if (concurrentDBMap.count(dbName) == 1) {
    return SQLiteOPResult{
        .type = SQLiteError,
        .errorMessage = dbName + " is already open",
    };
  }

  concurrentDBMap[dbName] = new ConnectionPool(dbName, docPath);
  concurrentDBMap[dbName]->setOnContextAvailable(contextAvailableCallback);
  concurrentDBMap[dbName]->setTableUpdateHandler(updateTableCallback);

  return SQLiteOPResult{
      .type = SQLiteOk,
  };
}

SQLiteOPResult sqliteExecuteInContext(std::string dbName,
                                      ConnectionLockId const contextId,
                                      string const &query,
                                      vector<QuickValue> *params,
                                      vector<map<string, QuickValue>> *results,
                                      vector<QuickColumnMetadata> *metadata) {
  if (concurrentDBMap.count(dbName) == 0) {
    return generateNotOpenResult(dbName);
  }

  ConnectionPool *connection = concurrentDBMap[dbName];
  return connection->executeInContext(contextId, query, params, results,
                                      metadata);
}

SQLiteOPResult sqliteCloseDBConcurrent(string const dbName) {
  if (concurrentDBMap.count(dbName) == 0) {
    return generateNotOpenResult(dbName);
  }

  ConnectionPool *connection = concurrentDBMap[dbName];

  connection->closeAll();
  delete connection;
  concurrentDBMap.erase(dbName);

  return SQLiteOPResult{
      .type = SQLiteOk,
  };
}

SQLiteOPResult sqliteConcurrentRequestLock(std::string const dbName,
                                           ConnectionLockId const contextId,
                                           ConcurrentLockType lockType) {
  if (concurrentDBMap.count(dbName) == 0) {
    return generateNotOpenResult(dbName);
  }

  ConnectionPool *connection = concurrentDBMap[dbName];

  switch (lockType) {
  case ConcurrentLockType::ReadLock:
    connection->readLock(contextId);
    break;
  case ConcurrentLockType::WriteLock:
    connection->writeLock(contextId);
    break;

  default:
    break;
  }

  return SQLiteOPResult{
      .type = SQLiteOk,
  };
}

void sqliteConcurrentReleaseLock(std::string const dbName,
                                 ConnectionLockId const contextId) {
  if (concurrentDBMap.count(dbName) == 0) {
    // Do nothing if the lock does not actually exist
    return;
  }

  ConnectionPool *connection = concurrentDBMap[dbName];
  connection->closeContext(contextId);
}

void closeAllConcurrentDBs() {
  for (auto const &x : concurrentDBMap) {
    x.second->closeAll();
    delete x.second;
  }
  concurrentDBMap.clear();
}
