/*
 * sequel.cpp
 *
 * Created by Oscar Franco on 2021/03/07
 * Copyright (c) 2021 Oscar Franco
 *
 * This code is licensed under the MIT license
 */

#include "sqliteBridge.h"
#include "ConnectionPool.h"
#include "fileUtils.h"
#include "logs.h"
#include <ctime>
#include <iostream>
#include <map>
#include <sqlite3.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;
using namespace facebook;

std::map<std::string, ConnectionPool *> concurrentDBMap =
    std::map<std::string, ConnectionPool *>();

SQLiteOPResult generateNotOpenResult(std::string const &dbName) {
  return SQLiteOPResult{
      .type = SQLiteError,
      .errorMessage = dbName + " is not open",
  };
}

/**
 * Opens SQL database with default settings
 */
SQLiteOPResult
sqliteOpenDb(string const dbName, string const docPath,
             void (*contextAvailableCallback)(std::string, ConnectionLockId),
             void (*updateTableCallback)(void *, int, const char *,
                                         const char *, sqlite3_int64)) {
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

SQLiteOPResult sqliteCloseDb(string const dbName) {
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

void sqliteCloseAll() {
  for (auto const &x : concurrentDBMap) {
    x.second->closeAll();
    delete x.second;
  }
  concurrentDBMap.clear();
}

SQLiteOPResult
sqliteExecuteInContext(std::string dbName, ConnectionLockId const contextId,
                       std::string const &query,
                       std::vector<QuickValue> *params,
                       std::vector<map<string, QuickValue>> *results,
                       std::vector<QuickColumnMetadata> *metadata) {
  if (concurrentDBMap.count(dbName) == 0) {
    return generateNotOpenResult(dbName);
  }

  ConnectionPool *connection = concurrentDBMap[dbName];
  return connection->executeInContext(contextId, query, params, results,
                                      metadata);
}

SequelLiteralUpdateResult
sqliteExecuteLiteralInContext(std::string dbName,
                              ConnectionLockId const contextId,
                              std::string const &query) {
  if (concurrentDBMap.count(dbName) == 0) {
    return {SQLiteError,
            "[react-native-quick-sqlite] SQL execution error: " + dbName +
                " is not open.",
            0};
  }

  ConnectionPool *connection = concurrentDBMap[dbName];
  return connection->executeLiteralInContext(contextId, query);
}

void sqliteReleaseLock(std::string const dbName,
                       ConnectionLockId const contextId) {
  if (concurrentDBMap.count(dbName) == 0) {
    // Do nothing if the lock does not actually exist
    return;
  }

  ConnectionPool *connection = concurrentDBMap[dbName];
  connection->closeContext(contextId);
}

SQLiteOPResult sqliteRequestLock(std::string const dbName,
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

SQLiteOPResult sqliteAttachDb(string const mainDBName, string const docPath,
                              string const databaseToAttach,
                              string const alias) {
  if (concurrentDBMap.count(mainDBName) == 0) {
    return generateNotOpenResult(mainDBName);
  }

  ConnectionPool *connection = concurrentDBMap[mainDBName];
  return connection->attachDatabase(databaseToAttach, docPath, alias);
}

SQLiteOPResult sqliteDetachDb(string const mainDBName, string const alias) {
  if (concurrentDBMap.count(mainDBName) == 0) {
    return generateNotOpenResult(mainDBName);
  }

  ConnectionPool *connection = concurrentDBMap[mainDBName];
  return connection->detachDatabase(alias);
}

SQLiteOPResult sqliteRemoveDb(string const dbName, string const docPath) {
  if (concurrentDBMap.count(dbName) == 1) {
    SQLiteOPResult closeResult = sqliteCloseDb(dbName);
    if (closeResult.type == SQLiteError) {
      return closeResult;
    }
  }

  string dbPath = get_db_path(dbName, docPath);

  if (!file_exists(dbPath)) {
    return SQLiteOPResult{
        .type = SQLiteOk,
        .errorMessage =
            "[react-native-quick-sqlite]: Database file not found" + dbPath};
  }

  remove(dbPath.c_str());

  return SQLiteOPResult{
      .type = SQLiteOk,
  };
}

/**
 * This should only be triggered once in a valid lock context. JSI bridge will
 * handle synchronization
 */
SequelBatchOperationResult sqliteImportFile(std::string const dbName,
                                            std::string const fileLocation) {
  if (concurrentDBMap.count(dbName) == 1) {
    SQLiteOPResult closeResult = sqliteCloseDb(dbName);
    if (closeResult.type == SQLiteError) {
      return SequelBatchOperationResult{
          .type = SQLiteError,
          .message = "DB is not open",
      };
    }
  }

  ConnectionPool *connection = concurrentDBMap[dbName];
  return connection->importSQLFile(fileLocation);
}
