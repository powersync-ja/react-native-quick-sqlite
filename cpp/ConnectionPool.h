#include "ConnectionState.h"
#include "JSIHelper.h"
#include "sqlite3.h"
#include <string>
#include <vector>
#include <future>

#ifndef ConnectionPool_h
#define ConnectionPool_h

enum TransactionEvent { COMMIT, ROLLBACK };

struct TransactionCallbackPayload {
  std::string *dbName;
  TransactionEvent event;
};

// The number of concurrent read connections to the database.
/**
 * Concurrent connection pool class.
 *
 * This allows for multiple (currently READ_CONNECTIONS) read connections and a
 * single write connection to operate concurrently.
 *
 * The SQLite database connections are opened in WAL mode, allowing for
 * concurrent reads and writes.
 *
 * This class acts as a simple state manager allowing requests for read and
 * write connections. The class will queue requests for connections and will
 * notify them they become available.
 *
 * Operations requesting locks here are synchronous and should be executed on a
 * single thread, however once a lock is active the connections can be used in a
 * thread pool for async statement executions.
 *
 * Synchronization, callback queueing and executions are managed by the
 * JavaScript portion of the library.
 *
 * The general flow is:
 *  + JavaScript requests read/write lock on a connection via
 * readLock/writeLock. JS provides a unique context ID for the lock
 * ------> If a connection is available: The SQLite connection is locked to the
 * ....... connection lock context ID provided by the requestor. The JavaScript
 * ....... bridge is informed from the Connection pool that the requested
 * ....... context lock ID is now active and SQL requests can be made with the
 * ....... context ID (on the relevant connection).
 * ------> If no connections are available, the context ID is added to a FIFO
 * ....... queue. Once other requests are completed the JavaScript bridge is
 * ....... informed that the requested context lock ID is now active.
 *
 *  + Any SQL requests are triggered (at the correct time) from the JavaScript
 * callback. Those requests are synchronized by returning JSI promises over the
 * bridge.
 *
 * + The JavaScript bridge makes a request to release the lock on the connection
 * pool once it's async callback operations are either resolved or rejected.
 */
class ConnectionPool {
private:
  int maxReads;
  std::string dbName;
  ConnectionState **readConnections;
  ConnectionState writeConnection;

  std::vector<ConnectionLockId> readQueue;
  std::vector<ConnectionLockId> writeQueue;

  // Cached constant payloads for c style commit/rollback callbacks
  TransactionCallbackPayload commitPayload;
  TransactionCallbackPayload rollbackPayload;

  void (*onContextCallback)(std::string, ConnectionLockId);
  void (*onCommitCallback)(const TransactionCallbackPayload *);

  bool isConcurrencyEnabled;

public:
  ConnectionPool(std::string dbName, std::string docPath,
                 unsigned int numReadConnections);
  ~ConnectionPool();

  friend int onCommitIntermediate(ConnectionPool *pool);

  /**
   * Add a task to the read queue. If there are no available connections,
   * the task will be queued.
   */
  void readLock(ConnectionLockId contextId);

  /**
   * Add a task to the write queue.
   */
  void writeLock(ConnectionLockId contextId);

  /**
   * Queue in context
   */
  SQLiteOPResult queueInContext(ConnectionLockId contextId,
                                std::function<void(sqlite3 *)> task);

  /**
   * Callback function when a new context is available for use
   */
  void setOnContextAvailable(void (*callback)(std::string, ConnectionLockId));

  /**
   * Set a callback function for table updates
   */
  void setTableUpdateHandler(void (*callback)(void *, int, const char *,
                                              const char *, sqlite3_int64));

  /**
   * Set a callback function for transaction commits/rollbacks
   */
  void setTransactionFinalizerHandler(
      void (*callback)(const TransactionCallbackPayload *));

  /**
   * Close a context in order to progress queue
   */
  void closeContext(ConnectionLockId contextId);

  // void closeContext(ConnectionLockContext *context);

  /**
   * Close all connections.
   */
  void closeAll();

  /**
   * Refreshes the schema for all connections.
   */
  std::future<void> refreshSchema();

  /**
   * Attaches another database to all connections
   */
  SQLiteOPResult attachDatabase(std::string const dbFileName,
                                std::string const docPath,
                                std::string const alias);

  SQLiteOPResult detachDatabase(std::string const alias);

private:
  std::vector<ConnectionState *> getAllConnections();

  void activateContext(ConnectionState &state, ConnectionLockId contextId);

  SQLiteOPResult genericSqliteOpenDb(string const dbName, string const docPath,
                                     sqlite3 **db, int sqlOpenFlags);
};

int onCommitIntermediate(ConnectionPool *pool);

#endif