#include "JSIHelper.h"
#include "sqlite3.h"
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <future>

#ifndef ConnectionState_h
#define ConnectionState_h

typedef std::string ConnectionLockId;

class ConnectionState {
public:
  // Only to be used by connection pool under some circumstances
  sqlite3 *connection;

private:
  ConnectionLockId _currentLockId;
  // Queue of requests waiting to be processed
  std::queue<std::function<void(sqlite3 *)>> workQueue;
  // Mutex to protect workQueue
  std::mutex workQueueMutex;
  // Store thread in order to stop it gracefully
  std::thread *thread;
  // This condition variable is used for the threads to wait until there is work
  // to do
  std::condition_variable_any workQueueConditionVariable;
  bool threadBusy;
  bool threadDone;

public:
  bool isClosed;

  ConnectionState(const std::string dbName, const std::string docPath,
                  int SQLFlags);
  ~ConnectionState();

  void clearLock();
  void activateLock(const ConnectionLockId &lockId);
  bool matchesLock(const ConnectionLockId &lockId);
  bool isEmptyLock();

  std::future<void> refreshSchema();
  void close();
  void queueWork(std::function<void(sqlite3 *)> task);

private:
  void doWork();
  void waitFinished();
};

#endif
