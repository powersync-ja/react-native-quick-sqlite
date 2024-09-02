#include "bindings.h"
#include "ConnectionPool.h"
#include "JSIHelper.h"
#include "logs.h"
#include "macros.h"
#include "sqlbatchexecutor.h"
#include "sqlite3.h"
#include "sqliteBridge.h"
#include "sqliteExecute.h"
#include <iostream>
#include <string>
#include <vector>

using namespace std;
using namespace facebook;

namespace osp {
string docPathStr;
std::shared_ptr<react::CallInvoker> invoker;
jsi::Runtime *runtime;

extern "C" {
int sqlite3_powersync_init(sqlite3 *db, char **pzErrMsg,
                           const sqlite3_api_routines *pApi);
}

/**
 * This function loads the PowerSync extension into SQLite
 */
int init_powersync_sqlite_plugin() {
  int result =
      sqlite3_auto_extension((void (*)(void)) & sqlite3_powersync_init);
  return result;
}

void osp::clearState() { sqliteCloseAll(); }

/**
 * Callback handler for SQLite table updates
 */
void updateTableHandler(void *voidDBName, int opType, char const *dbName,
                        char const *tableName, sqlite3_int64 rowId) {
  /**
   * No DB operations should occur when this callback is fired from SQLite.
   * This function triggers an async invocation to call watch callbacks,
   * avoiding holding SQLite up.
   */
  invoker->invokeAsync([voidDBName, opType, dbName, tableName, rowId] {
    try {
      // Sqlite 3 just returns main as the db name if no other DBs are attached
      auto global = runtime->global();
      jsi::Function handlerFunction =
          global.getPropertyAsFunction(*runtime, "triggerUpdateHook");

      std::string actualDBName = std::string((char *)voidDBName);
      auto jsiDbName = jsi::String::createFromAscii(*runtime, actualDBName);
      auto jsiTableName = jsi::String::createFromAscii(*runtime, tableName);
      auto jsiOpType = jsi::Value(opType);
      auto jsiRowId =
          jsi::String::createFromAscii(*runtime, std::to_string(rowId));
      handlerFunction.call(*runtime, move(jsiDbName), move(jsiTableName),
                           move(jsiOpType), move(jsiRowId));
    } catch (jsi::JSINativeException e) {
      std::cout << e.what() << std::endl;
    } catch (...) {
      std::cout << "Unknown error" << std::endl;
    }
  });
}

/**
 * Callback handler for SQLite transaction updates
 */
void transactionFinalizerHandler(const TransactionCallbackPayload *payload) {
  /**
   * No DB operations should occur when this callback is fired from SQLite.
   * This function triggers an async invocation to call watch callbacks,
   * avoiding holding SQLite up.
   */
  invoker->invokeAsync([payload] {
    try {
      auto global = runtime->global();
      jsi::Function handlerFunction = global.getPropertyAsFunction(
          *runtime, "triggerTransactionFinalizerHook");

      auto jsiDbName = jsi::String::createFromAscii(*runtime, *payload->dbName);
      auto jsiEventType = jsi::Value((int)payload->event);
      handlerFunction.call(*runtime, move(jsiDbName), move(jsiEventType));
    } catch (jsi::JSINativeException e) {
      std::cout << e.what() << std::endl;
    } catch (...) {
      std::cout << "Unknown error" << std::endl;
    }
  });
}

/**
 * Callback handler for Concurrent context is available
 */
void contextLockAvailableHandler(std::string dbName,
                                 ConnectionLockId contextId) {
  invoker->invokeAsync([dbName, contextId] {
    try {
      auto global = runtime->global();
      jsi::Function handlerFunction =
          global.getPropertyAsFunction(*runtime, "onLockContextIsAvailable");

      auto jsiDBName = jsi::String::createFromAscii(*runtime, dbName);
      auto jsiLockID = jsi::String::createFromAscii(*runtime, contextId);
      handlerFunction.call(*runtime, move(jsiDBName), move(jsiLockID));
    } catch (jsi::JSINativeException e) {
      std::cout << e.what() << std::endl;
    } catch (...) {
      std::cout << "[contextLockAvailableHandler]: Unknown error" << std::endl;
    }
  });
}

void osp::install(jsi::Runtime &rt,
                  std::shared_ptr<react::CallInvoker> jsCallInvoker,
                  const char *docPath) {
  docPathStr = std::string(docPath);
  invoker = jsCallInvoker;
  runtime = &rt;

  // Any DBs opened after this call will have PowerSync SQLite extension loaded
  init_powersync_sqlite_plugin();

  auto open = HOSTFN("open", 2) {

    if (count == 0) {
      throw jsi::JSError(
          rt, "[react-native-quick-sqlite][open] database name is required");
    }

    if (!args[0].isString()) {
      throw jsi::JSError(
          rt,
          "[react-native-quick-sqlite][open] database name must be a string");
    }

    string dbName = args[0].asString(rt).utf8(rt);
    string tempDocPath = string(docPathStr);
    unsigned int numReadConnections = 0;

    if (count > 1 && !args[1].isUndefined() && !args[1].isNull()) {
      if (!args[1].isObject()) {
        throw jsi::JSError(rt, "[react-native-quick-sqlite][open] database "
                               "options must be an object");
      }

      auto options = args[1].asObject(rt);
      auto numReadConnectionsProperty =
          options.getProperty(rt, "numReadConnections");
      if (!numReadConnectionsProperty.isUndefined()) {
        numReadConnections = numReadConnectionsProperty.asNumber();
      }

      auto locationPropertyProperty = options.getProperty(rt, "location");
      if (!locationPropertyProperty.isUndefined() &&
          !locationPropertyProperty.isNull()) {
        tempDocPath =
            tempDocPath + "/" + locationPropertyProperty.asString(rt).utf8(rt);
      }
    }

    auto result = sqliteOpenDb(
        dbName, tempDocPath, &contextLockAvailableHandler, &updateTableHandler,
        &transactionFinalizerHandler, numReadConnections);
    if (result.type == SQLiteError) {
      throw jsi::JSError(rt, result.errorMessage.c_str());
    }

    return {};
  });

  auto attach = HOSTFN("attach", 4) {
    if (count < 3) {
      throw jsi::JSError(
          rt,
          "[react-native-quick-sqlite][attach] Incorrect number of arguments");
    }
    if (!args[0].isString() || !args[1].isString() || !args[2].isString()) {
      throw jsi::JSError(
          rt, "dbName, databaseToAttach and alias must be a strings");
      return {};
    }

    string tempDocPath = string(docPathStr);
    if (count > 3 && !args[3].isUndefined() && !args[3].isNull()) {
      if (!args[3].isString()) {
        throw jsi::JSError(rt, "[react-native-quick-sqlite][attach] database "
                               "location must be a string");
      }

      tempDocPath = tempDocPath + "/" + args[3].asString(rt).utf8(rt);
    }
    string dbName = args[0].asString(rt).utf8(rt);
    string databaseToAttach = args[1].asString(rt).utf8(rt);
    string alias = args[2].asString(rt).utf8(rt);
    SQLiteOPResult result =
        sqliteAttachDb(dbName, tempDocPath, databaseToAttach, alias);

    if (result.type == SQLiteError) {
      throw jsi::JSError(rt, result.errorMessage.c_str());
    }

    return {};
  });

  auto detach = HOSTFN("detach", 2) {
    if (count < 2) {
      throw jsi::JSError(
          rt,
          "[react-native-quick-sqlite][detach] Incorrect number of arguments");
    }
    if (!args[0].isString() || !args[1].isString()) {
      throw jsi::JSError(
          rt, "dbName, databaseToAttach and alias must be a strings");
      return {};
    }

    string dbName = args[0].asString(rt).utf8(rt);
    string alias = args[1].asString(rt).utf8(rt);
    SQLiteOPResult result = sqliteDetachDb(dbName, alias);

    if (result.type == SQLiteError) {
      throw jsi::JSError(rt, result.errorMessage.c_str());
    }

    return {};
  });

  auto close = HOSTFN("close", 1) {
    if (count == 0) {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][closeConcurrent] "
                             "database name is required");
    }

    if (!args[0].isString()) {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][closeConcurrent] "
                             "database name must be a string");
    }

    string dbName = args[0].asString(rt).utf8(rt);

    SQLiteOPResult result = sqliteCloseDb(dbName);

    if (result.type == SQLiteError) {
      throw jsi::JSError(rt, result.errorMessage.c_str());
    }

    return {};
  });

  auto remove = HOSTFN("delete", 2) {
    if (count == 0) {
      throw jsi::JSError(
          rt, "[react-native-quick-sqlite][open] database name is required");
    }

    if (!args[0].isString()) {
      throw jsi::JSError(
          rt,
          "[react-native-quick-sqlite][open] database name must be a string");
    }

    string dbName = args[0].asString(rt).utf8(rt);

    string tempDocPath = string(docPathStr);
    if (count > 1 && !args[1].isUndefined() && !args[1].isNull()) {
      if (!args[1].isString()) {
        throw jsi::JSError(rt, "[react-native-quick-sqlite][open] database "
                               "location must be a string");
      }

      tempDocPath = tempDocPath + "/" + args[1].asString(rt).utf8(rt);
    }

    SQLiteOPResult result = sqliteRemoveDb(dbName, tempDocPath);

    if (result.type == SQLiteError) {
      throw jsi::JSError(rt, result.errorMessage.c_str());
    }

    return {};
  });

  auto executeInContext = HOSTFN("executeInContext", 3) {
    if (count < 4) {
      throw jsi::JSError(rt,
                         "[react-native-quick-sqlite][executeInContextAsync] "
                         "Incorrect arguments for executeInContextAsync");
    }

    const string dbName = args[0].asString(rt).utf8(rt);
    const string contextLockId = args[1].asString(rt).utf8(rt);
    const string query = args[2].asString(rt).utf8(rt);
    const jsi::Value &originalParams = args[3];

    // Converting query parameters inside the javascript caller thread
    vector<QuickValue> params;
    jsiQueryArgumentsToSequelParam(rt, originalParams, &params);

    auto promiseCtr = rt.global().getPropertyAsFunction(rt, "Promise");
    auto promise = promiseCtr.callAsConstructor(rt, HOSTFN("executor", 2) {
      auto resolve = std::make_shared<jsi::Value>(rt, args[0]);
      auto reject = std::make_shared<jsi::Value>(rt, args[1]);

      auto task = [&rt, dbName, contextLockId, query,
                   params = make_shared<vector<QuickValue>>(params), resolve,
                   reject](sqlite3 *db) {
        try {
          vector<map<string, QuickValue>> results;
          vector<QuickColumnMetadata> metadata;
          auto status =
              sqliteExecuteWithDB(db, query, params.get(), &results, &metadata);
          invoker->invokeAsync(
              [&rt,
               results = make_shared<vector<map<string, QuickValue>>>(results),
               metadata = make_shared<vector<QuickColumnMetadata>>(metadata),
               status_copy = move(status), resolve, reject] {
                if (status_copy.type == SQLiteOk) {
                  auto jsiResult = createSequelQueryExecutionResult(
                      rt, status_copy, results.get(), metadata.get());
                  resolve->asObject(rt).asFunction(rt).call(rt,
                                                            move(jsiResult));
                } else {
                  auto errorCtr =
                      rt.global().getPropertyAsFunction(rt, "Error");
                  auto error = errorCtr.callAsConstructor(
                      rt, jsi::String::createFromUtf8(
                              rt, status_copy.errorMessage));
                  reject->asObject(rt).asFunction(rt).call(rt, error);
                }
              });
        } catch (std::exception &exc) {
          invoker->invokeAsync([&rt, &exc] { jsi::JSError(rt, exc.what()); });
        }
      };

      sqliteQueueInContext(dbName, contextLockId, task);
      return {};
    }));

    return promise;
  });

  auto executeBatch = HOSTFN("executeBatch", 2) {
    if (sizeof(args) < 3) {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][executeAsyncBatch] "
                             "Incorrect parameter count");
      return {};
    }

    const jsi::Value &params = args[1];

    if (params.isNull() || params.isUndefined()) {
      throw jsi::JSError(rt,
                         "[react-native-quick-sqlite][executeAsyncBatch] - An "
                         "array of SQL commands or parameters is needed");
      return {};
    }

    const string dbName = args[0].asString(rt).utf8(rt);
    const jsi::Array &batchParams = params.asObject(rt).asArray(rt);
    const string contextLockId = args[2].asString(rt).utf8(rt);

    vector<QuickQueryArguments> commands;
    jsiBatchParametersToQuickArguments(rt, batchParams, &commands);

    auto promiseCtr = rt.global().getPropertyAsFunction(rt, "Promise");
    auto promise = promiseCtr.callAsConstructor(rt, HOSTFN("executor", 2) {
      auto resolve = std::make_shared<jsi::Value>(rt, args[0]);
      auto reject = std::make_shared<jsi::Value>(rt, args[1]);

      auto task = [&rt, dbName,
                   commands =
                       make_shared<vector<QuickQueryArguments>>(commands),
                   resolve, reject, contextLockId](sqlite3 *db) {
        try {
          // Inside the new worker thread, we can now call sqlite operations
          auto batchResult = sqliteExecuteBatch(db, commands.get());
          invoker->invokeAsync(
              [&rt, batchResult = move(batchResult), resolve, reject] {
                if (batchResult.type == SQLiteOk) {
                  auto res = jsi::Object(rt);
                  res.setProperty(rt, "rowsAffected",
                                  jsi::Value(batchResult.affectedRows));
                  resolve->asObject(rt).asFunction(rt).call(rt, move(res));
                } else {
                  throw jsi::JSError(rt, batchResult.message);
                }
              });
        } catch (std::exception &exc) {
          invoker->invokeAsync(
              [&rt, reject, &exc] { throw jsi::JSError(rt, exc.what()); });
        }
      };

      sqliteQueueInContext(dbName, contextLockId, task);
      return {};
    }));

    return promise;
  });

  // Load SQL File from disk in another thread
  auto loadFileAsync = HOSTFN("loadFile", 2) {
    if (sizeof(args) < 3) {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][loadFileAsync] "
                             "Incorrect parameter count");
      return {};
    }

    const string dbName = args[0].asString(rt).utf8(rt);
    const string sqlFileName = args[1].asString(rt).utf8(rt);
    const string contextLockId = args[2].asString(rt).utf8(rt);

    auto promiseCtr = rt.global().getPropertyAsFunction(rt, "Promise");
    auto promise = promiseCtr.callAsConstructor(rt, HOSTFN("executor", 2) {
      auto resolve = std::make_shared<jsi::Value>(rt, args[0]);
      auto reject = std::make_shared<jsi::Value>(rt, args[1]);

      auto task = [&rt, dbName, sqlFileName, resolve, reject](sqlite3 *db) {
        try {
          const auto importResult = sqliteImportFile(db, sqlFileName);

          invoker->invokeAsync(
              [&rt, result = move(importResult), resolve, reject] {
                if (result.type == SQLiteOk) {
                  auto res = jsi::Object(rt);
                  res.setProperty(rt, "rowsAffected",
                                  jsi::Value(result.affectedRows));
                  res.setProperty(rt, "commands", jsi::Value(result.commands));
                  resolve->asObject(rt).asFunction(rt).call(rt, move(res));
                } else {
                  throw jsi::JSError(rt, result.message);
                }
              });
        } catch (std::exception &exc) {
          //          LOGW("Catched exception: %s", exc.what());
          invoker->invokeAsync(
              [&rt, err = exc.what(), reject] { throw jsi::JSError(rt, err); });
        }
      };
      sqliteQueueInContext(dbName, contextLockId, task);
      return {};
    }));

    return promise;
  });

  auto requestLock = HOSTFN("requestLock", 3) {
    if (count < 3) {
      throw jsi::JSError(rt,
                         "[react-native-quick-sqlite][requestLock] "
                         "database name, lock ID and lock type are required");
    }

    if (!args[0].isString() || !args[1].isString() || !args[2].isNumber()) {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][requestLock] "
                             "invalid argument types received");
    }

    string dbName = args[0].asString(rt).utf8(rt);
    string lockId = args[1].asString(rt).utf8(rt);
    ConcurrentLockType lockType = (ConcurrentLockType)args[2].asNumber();

    auto lockResult = sqliteRequestLock(dbName, lockId, lockType);
    vector<map<string, QuickValue>> resultsHolder;
    auto jsiResult =
        createSequelQueryExecutionResult(rt, lockResult, &resultsHolder, NULL);
    return jsiResult;
  });

  auto releaseLock = HOSTFN("releaseLock", 3) {
    if (count < 2) {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][requestLock] "
                             "database name and lock ID  are required");
    }

    if (!args[0].isString() || !args[1].isString()) {
      throw jsi::JSError(rt, "[react-native-quick-sqlite][requestLock] "
                             "invalid argument types received");
    }

    string dbName = args[0].asString(rt).utf8(rt);
    string lockId = args[1].asString(rt).utf8(rt);

    sqliteReleaseLock(dbName, lockId);

    return {};
  });

  jsi::Object module = jsi::Object(rt);

  module.setProperty(rt, "open", move(open));
  module.setProperty(rt, "requestLock", move(requestLock));
  module.setProperty(rt, "releaseLock", move(releaseLock));
  module.setProperty(rt, "executeInContext", move(executeInContext));
  module.setProperty(rt, "close", move(close));

  module.setProperty(rt, "attach", move(attach));
  module.setProperty(rt, "detach", move(detach));
  module.setProperty(rt, "delete", move(remove));
  module.setProperty(rt, "executeBatch", move(executeBatch));
  module.setProperty(rt, "loadFileAsync", move(loadFileAsync));

  rt.global().setProperty(rt, "__QuickSQLiteProxy", move(module));
}

} // namespace osp
