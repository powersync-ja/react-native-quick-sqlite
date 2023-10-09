
#include "ConnectionPool.h"
#include "JSIHelper.h"

enum ConcurrentLockType {
  ReadLock,
  WriteLock,
};

/**
 * Opens a concurrent DB pool with multiple read and a single
 * write connection
 */
SQLiteOPResult sqliteOpenDBConcurrent(
    string const dbName, string const docPath,
    void (*contextAvailableCallback)(std::string, ConnectionLockId),
    void (*updateTableCallback)(void *, int, const char *, const char *,
                                sqlite3_int64));

/**
 * Closes a concurrent DB pool
 */
SQLiteOPResult sqliteCloseDBConcurrent(std::string const dbName);

void closeAllConcurrentDBs();

SQLiteOPResult sqliteExecuteInContext(std::string dbName,
                                      ConnectionLockId const contextId,
                                      string const &query,
                                      vector<QuickValue> *params,
                                      vector<map<string, QuickValue>> *results,
                                      vector<QuickColumnMetadata> *metadata);

void sqliteConcurrentReleaseLock(std::string const dbName,
                                 ConnectionLockId const contextId);

/**
 * Requests a lock context to be queued for a read or write lock
 */
SQLiteOPResult sqliteConcurrentRequestLock(std::string const dbName,
                                           ConnectionLockId const contextId,
                                           ConcurrentLockType lockType);
