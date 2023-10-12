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

/**
 * TODO funnel this though to all connection pool's connections
 */
SQLiteOPResult sqliteAttachDb(string const mainDBName, string const docPath,
                              string const databaseToAttach,
                              string const alias) {
  /**
   * There is no need to check if mainDBName is opened because
   * sqliteExecuteLiteral will do that.
   * */
  string dbPath = get_db_path(databaseToAttach, docPath);
  string statement = "ATTACH DATABASE '" + dbPath + "' AS " + alias;
  SequelLiteralUpdateResult result =
      sqliteExecuteLiteral(mainDBName, statement);
  if (result.type == SQLiteError) {
    return SQLiteOPResult{
        .type = SQLiteError,
        .errorMessage =
            mainDBName +
            " was unable to attach another database: " + string(result.message),
    };
  }
  return SQLiteOPResult{
      .type = SQLiteOk,
  };
}

/**
 * TODO funnel this though to all connection pool's connections
 */
SQLiteOPResult sqliteDetachDb(string const mainDBName, string const alias) {
  /**
   * There is no need to check if mainDBName is opened because
   * sqliteExecuteLiteral will do that.
   * */
  string statement = "DETACH DATABASE " + alias;
  SequelLiteralUpdateResult result =
      sqliteExecuteLiteral(mainDBName, statement);
  if (result.type == SQLiteError) {
    return SQLiteOPResult{
        .type = SQLiteError,
        .errorMessage = mainDBName + "was unable to detach database: " +
                        string(result.message),
    };
  }
  return SQLiteOPResult{
      .type = SQLiteOk,
  };
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
