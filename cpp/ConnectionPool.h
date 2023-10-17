#include "JSIHelper.h"
#include "ThreadPool.h"
#include "sqlite3.h"
#include <string>
#include <vector>

#ifndef ConnectionPool_h
#define ConnectionPool_h

// The number of concurrent read connections to the database.
#define EMPTY_LOCK_ID ""

typedef std::string ConnectionLockId;

struct ConnectionState {
  sqlite3 *connection;
  ConnectionLockId currentLockId;
};

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
  std::vector<ConnectionState> readConnections;
  ConnectionState writeConnection;

  std::vector<ConnectionLockId> readQueue;
  std::vector<ConnectionLockId> writeQueue;

  void (*onContextCallback)(std::string, ConnectionLockId);

  bool isConcurrencyEnabled;

public:
  ConnectionPool(std::string dbName, std::string docPath,
                 unsigned int numReadConnections);
  ~ConnectionPool();

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
   * Execute in context
   */
  SQLiteOPResult executeInContext(ConnectionLockId contextId,
                                  string const &query,
                                  vector<QuickValue> *params,
                                  vector<map<string, QuickValue>> *results,
                                  vector<QuickColumnMetadata> *metadata);

  /**
   * Execute in context
   */
  SequelLiteralUpdateResult executeLiteralInContext(ConnectionLockId contextId,
                                                    string const &query);

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
   * Close a context in order to progress queue
   */
  void closeContext(ConnectionLockId contextId);

  // void closeContext(ConnectionLockContext *context);

  /**
   * Close all connections.
   */
  void closeAll();

  /**
   * Attaches another database to all connections
   */
  SQLiteOPResult attachDatabase(std::string const dbFileName,
                                std::string const docPath,
                                std::string const alias);

  SQLiteOPResult detachDatabase(std::string const alias);

  /**
   * Executes commands from a SQLite file
   */
  SequelBatchOperationResult importSQLFile(std::string fileLocation);

private:
  std::vector<sqlite3 *> getAllConnections();

  void activateContext(ConnectionState *state, ConnectionLockId contextId);

  SQLiteOPResult genericSqliteOpenDb(string const dbName, string const docPath,
                                     sqlite3 **db, int sqlOpenFlags);
};

#endif