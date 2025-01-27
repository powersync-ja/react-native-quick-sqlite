#include "ConnectionState.h"
#include "fileUtils.h"
#include "sqlite3.h"

const std::string EMPTY_LOCK_ID = "";

SQLiteOPResult genericSqliteOpenDb(string const dbName, string const docPath,
                                   sqlite3 **db, int sqlOpenFlags);

ConnectionState::ConnectionState(const std::string dbName,
                                 const std::string docPath, int SQLFlags) {
  auto result = genericSqliteOpenDb(dbName, docPath, &connection, SQLFlags);
   if (result.type != SQLiteOk) {
    throw std::runtime_error("Failed to open SQLite database: " + result.errorMessage);
  }
   thread = std::thread(&ConnectionState::doWork, this);
   this->clearLock();
}

ConnectionState::~ConnectionState() {
  if (!isClosed) {
    close();
  }
}

void ConnectionState::clearLock() {
  waitFinished();
  _currentLockId = EMPTY_LOCK_ID;
}

void ConnectionState::activateLock(const ConnectionLockId &lockId) {
  _currentLockId = lockId;
}

bool ConnectionState::matchesLock(const ConnectionLockId &lockId) {
  return _currentLockId == lockId;
}

bool ConnectionState::isEmptyLock() { return _currentLockId == EMPTY_LOCK_ID; }

std::future<void> ConnectionState::refreshSchema() {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    queueWork([promise](sqlite3* db) {
        try {
            int rc = sqlite3_exec(db, "PRAGMA table_info('sqlite_master')", nullptr, nullptr, nullptr);
            if (rc != SQLITE_OK) {
                throw std::runtime_error("Failed to refresh schema");
            }
            promise->set_value();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });

    return future;
}

void ConnectionState::close() {
  {
    std::unique_lock<std::mutex> g(workQueueMutex);
    // prevent any new work from being queued 
    isClosed = true;
  }

  // Wait for the work queue to empty
  waitFinished();

  {
    // Now signal the thread to stop and notify it
    std::unique_lock<std::mutex> g(workQueueMutex);
    threadDone = true;
    workQueueConditionVariable.notify_all();
  }

  // Join the worker thread
  if (thread.joinable()) {
    thread.join();
  }

  // Safely close the SQLite connection
  sqlite3_close_v2(connection);
}

void ConnectionState::queueWork(std::function<void(sqlite3 *)> task) {
  {
    std::unique_lock<std::mutex> g(workQueueMutex);
    if (isClosed) {
      throw std::runtime_error("Connection is not open. Connection has been closed before queueing work.");
    }
    workQueue.push(task);
  }

  workQueueConditionVariable.notify_all(); 
}

void ConnectionState::doWork() {
  // Loop while the queue is not destructing
  while (!threadDone) {
    std::function<void(sqlite3 *)> task;

    // Create a scope, so we don't lock the queue for longer than necessary
    {
      std::unique_lock<std::mutex> g(workQueueMutex);
      workQueueConditionVariable.wait(g, [&] {
        // Only wake up if there are elements in the queue or the program is
        // shutting down
        return !workQueue.empty() || threadDone;
      });

      // If we are shutting down exit without trying to process more work
      if (threadDone) {
        break;
      }

      task = workQueue.front();
      workQueue.pop();
    }

    threadBusy = true;
    task(connection);
    threadBusy = false;
    // Need to notify in order for waitFinished to be updated when
    // the queue is empty and not busy
    {
      std::unique_lock<std::mutex> g(workQueueMutex);
      workQueueConditionVariable.notify_all();
    }
  }
}

void ConnectionState::waitFinished() {
  std::unique_lock<std::mutex> g(workQueueMutex);
  workQueueConditionVariable.wait(
      g, [&] { return workQueue.empty() && !threadBusy; });
}

SQLiteOPResult genericSqliteOpenDb(string const dbName, string const docPath,
                                   sqlite3 **db, int sqlOpenFlags) {
  string dbPath = get_db_path(dbName, docPath);

  int exit = 0;
  exit = sqlite3_open_v2(dbPath.c_str(), db, sqlOpenFlags, nullptr);

  if (exit != SQLITE_OK) {
    return SQLiteOPResult{.type = SQLiteError,
                          .errorMessage = sqlite3_errmsg(*db)};
  }

  // Set journal mode directly when opening.
  // This may have some overhead on the main thread,
  // but prevents race conditions with multiple connections.
  if (sqlOpenFlags & SQLITE_OPEN_READONLY) {
    exit = sqlite3_exec(*db, "PRAGMA busy_timeout = 30000;"
      // Default to normal on all connections
      "PRAGMA synchronous = NORMAL;",
      nullptr, nullptr, nullptr
    );
  } else {
    exit = sqlite3_exec(*db, "PRAGMA busy_timeout = 30000;"
      "PRAGMA journal_mode = WAL;"
      // 6Mb 1.5x default checkpoint size
      "PRAGMA journal_size_limit = 6291456;"
      // Default to normal on all connections
      "PRAGMA synchronous = NORMAL;",
      nullptr, nullptr, nullptr
    );
  }
  if (exit != SQLITE_OK) {
    return SQLiteOPResult{.type = SQLiteError,
                          .errorMessage = sqlite3_errmsg(*db)};
  }

  return SQLiteOPResult{.type = SQLiteOk, .rowsAffected = 0};
}