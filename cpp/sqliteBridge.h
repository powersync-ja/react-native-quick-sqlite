/*
 * sequel.h
 *
 * Created by Oscar Franco on 2021/03/07
 * Copyright (c) 2021 Oscar Franco
 *
 * This code is licensed under the MIT license
 */

#include "ConnectionPool.h"
#include "JSIHelper.h"
#include "sqlite3.h"
#include <vector>

#ifndef SQLiteBridge_h
#define SQLiteBridge_h

#define CONCURRENT_READ_CONNECTIONS 4

using namespace facebook;

enum ConcurrentLockType {
  ReadLock,
  WriteLock,
};

SQLiteOPResult
sqliteOpenDb(std::string const dbName, std::string const docPath,
             void (*contextAvailableCallback)(std::string, ConnectionLockId),
             void (*updateTableCallback)(void *, int, const char *,
                                         const char *, sqlite3_int64),
             void (*onTransactionFinalizedCallback)(
                 const TransactionCallbackPayload *event),
             uint32_t numReadConnections);

void sqliteRefreshSchema(string const dbName);

SQLiteOPResult sqliteCloseDb(string const dbName);

void sqliteCloseAll();

SQLiteOPResult sqliteRemoveDb(string const dbName, string const docPath);

/**
 * Requests a lock context to be queued for a read or write lock
 */
SQLiteOPResult sqliteRequestLock(std::string const dbName,
                                 ConnectionLockId const contextId,
                                 ConcurrentLockType lockType);

SQLiteOPResult sqliteQueueInContext(std::string dbName,
                                    ConnectionLockId const contextId,
                                    std::function<void(sqlite3 *)>);

void sqliteReleaseLock(std::string const dbName,
                       ConnectionLockId const contextId);

SQLiteOPResult sqliteAttachDb(string const mainDBName, string const docPath,
                              string const databaseToAttach,
                              string const alias);

SQLiteOPResult sqliteDetachDb(string const mainDBName, string const alias);

#endif