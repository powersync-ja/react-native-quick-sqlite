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
#include "sqlite3.h"
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;
using namespace facebook;

std::map<std::string, ConnectionPool *> dbMap =
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
                                         const char *, sqlite3_int64),
             void (*onTransactionFinalizedCallback)(
                 const TransactionCallbackPayload *event),
             uint32_t numReadConnections) {
  if (dbMap.count(dbName) == 1) {
    return SQLiteOPResult{
        .type = SQLiteError,
        .errorMessage = dbName + " is already open",
    };
  }

  try {
    // Open the database
    dbMap[dbName] = new ConnectionPool(dbName, docPath, numReadConnections);
    dbMap[dbName]->setOnContextAvailable(contextAvailableCallback);
    dbMap[dbName]->setTableUpdateHandler(updateTableCallback);
    dbMap[dbName]->setTransactionFinalizerHandler(onTransactionFinalizedCallback);
  } catch (const std::exception &e) {
    return SQLiteOPResult{
        .type = SQLiteError,
        .errorMessage = e.what(),
    };
  }

  return SQLiteOPResult{
      .type = SQLiteOk,
  };
}

std::future<void> sqliteRefreshSchema(const std::string& dbName) {
    if (dbMap.count(dbName) == 0) {
        std::promise<void> promise;
        promise.set_value();
        return promise.get_future();
    }

    ConnectionPool* connection = dbMap[dbName];
    return connection->refreshSchema();
}

SQLiteOPResult sqliteCloseDb(string const dbName) {
  if (dbMap.count(dbName) == 0) {
    return generateNotOpenResult(dbName);
  }

  ConnectionPool *connection = dbMap[dbName];

  connection->closeAll();
  dbMap.erase(dbName);
  delete connection;

  return SQLiteOPResult{
      .type = SQLiteOk,
  };
}

void sqliteCloseAll() {
  for (auto const &x : dbMap) {
    x.second->closeAll();
    delete x.second;
  }
  dbMap.clear();
}

SQLiteOPResult sqliteQueueInContext(std::string dbName,
                                    ConnectionLockId const contextId,
                                    std::function<void(sqlite3 *)> task) {
  if (dbMap.count(dbName) == 0) {
    return generateNotOpenResult(dbName);
  }

  ConnectionPool *connection = dbMap[dbName];
  return connection->queueInContext(contextId, task);
}

void sqliteReleaseLock(std::string const dbName,
                       ConnectionLockId const contextId) {
  if (dbMap.count(dbName) == 0) {
    // Do nothing if the lock does not actually exist
    return;
  }

  ConnectionPool *connection = dbMap[dbName];
  connection->closeContext(contextId);
}

SQLiteOPResult sqliteRequestLock(std::string const dbName,
                                 ConnectionLockId const contextId,
                                 ConcurrentLockType lockType) {
  if (dbMap.count(dbName) == 0) {
    return generateNotOpenResult(dbName);
  }

  ConnectionPool *connection = dbMap[dbName];

  if (connection == nullptr) {
    return SQLiteOPResult{
        .type = SQLiteOk,

    };
  }

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
  if (dbMap.count(mainDBName) == 0) {
    return generateNotOpenResult(mainDBName);
  }

  ConnectionPool *connection = dbMap[mainDBName];
  return connection->attachDatabase(databaseToAttach, docPath, alias);
}

SQLiteOPResult sqliteDetachDb(string const mainDBName, string const alias) {
  if (dbMap.count(mainDBName) == 0) {
    return generateNotOpenResult(mainDBName);
  }

  ConnectionPool *connection = dbMap[mainDBName];
  return connection->detachDatabase(alias);
}

SQLiteOPResult sqliteRemoveDb(string const dbName, string const docPath) {
  if (dbMap.count(dbName) == 1) {
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
