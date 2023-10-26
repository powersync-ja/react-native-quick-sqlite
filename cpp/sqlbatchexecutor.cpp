/**
 * Batch execution implementation
 */
#include "sqlbatchexecutor.h"
#include "fileUtils.h"
#include "sqliteExecute.h"
#include <fstream>
#include <iostream>

void jsiBatchParametersToQuickArguments(jsi::Runtime &rt,
                                        jsi::Array const &batchParams,
                                        vector<QuickQueryArguments> *commands) {
  for (int i = 0; i < batchParams.length(rt); i++) {
    const jsi::Array &command =
        batchParams.getValueAtIndex(rt, i).asObject(rt).asArray(rt);
    if (command.length(rt) == 0) {
      continue;
    }

    const string query = command.getValueAtIndex(rt, 0).asString(rt).utf8(rt);
    const jsi::Value &commandParams = command.length(rt) > 1
                                          ? command.getValueAtIndex(rt, 1)
                                          : jsi::Value::undefined();
    if (!commandParams.isUndefined() &&
        commandParams.asObject(rt).isArray(rt) &&
        commandParams.asObject(rt).asArray(rt).length(rt) > 0 &&
        commandParams.asObject(rt)
            .asArray(rt)
            .getValueAtIndex(rt, 0)
            .isObject()) {
      // This arguments is an array of arrays, like a batch update of a single
      // sql command.
      const jsi::Array &batchUpdateParams =
          commandParams.asObject(rt).asArray(rt);
      for (int x = 0; x < batchUpdateParams.length(rt); x++) {
        const jsi::Value &p = batchUpdateParams.getValueAtIndex(rt, x);
        vector<QuickValue> params;
        jsiQueryArgumentsToSequelParam(rt, p, &params);
        commands->push_back(QuickQueryArguments{
            query, make_shared<vector<QuickValue>>(params)});
      }
    } else {
      vector<QuickValue> params;
      jsiQueryArgumentsToSequelParam(rt, commandParams, &params);
      commands->push_back(
          QuickQueryArguments{query, make_shared<vector<QuickValue>>(params)});
    }
  }
}

SequelBatchOperationResult
sqliteExecuteBatch(sqlite3 *db, vector<QuickQueryArguments> *commands) {
  size_t commandCount = commands->size();
  if (commandCount <= 0) {
    return SequelBatchOperationResult{
        .type = SQLiteError,
        .message = "No SQL commands provided",
    };
  }

  try {
    int affectedRows = 0;

    sqliteExecuteLiteralWithDB(db, "BEGIN EXCLUSIVE TRANSACTION");

    for (int i = 0; i < commandCount; i++) {
      auto command = commands->at(i);
      // We do not provide a datastructure to receive query data because we
      // don't need/want to handle this results in a batch execution
      auto result = sqliteExecuteWithDB(db, command.sql, command.params.get(),
                                        NULL, NULL);
      if (result.type == SQLiteError) {
        sqliteExecuteLiteralWithDB(db, "ROLLBACK");
        return SequelBatchOperationResult{
            .type = SQLiteError,
            .message = result.errorMessage,
        };
      } else {
        affectedRows += result.rowsAffected;
      }
    }
    sqliteExecuteLiteralWithDB(db, "COMMIT");
    return SequelBatchOperationResult{
        .type = SQLiteOk,
        .affectedRows = affectedRows,
        .commands = (int)commandCount,
    };
  } catch (std::exception &exc) {
    sqliteExecuteLiteralWithDB(db, "ROLLBACK");
    return SequelBatchOperationResult{
        .type = SQLiteError,
        .message = exc.what(),
    };
  }
}

SequelBatchOperationResult sqliteImportFile(sqlite3 *db,
                                            const std::string fileLocation) {
  std::string line;
  std::ifstream sqFile(fileLocation);

  if (sqFile.is_open()) {
    try {
      int affectedRows = 0;
      int commands = 0;
      sqliteExecuteLiteralWithDB(db, "BEGIN EXCLUSIVE TRANSACTION");
      while (std::getline(sqFile, line, '\n')) {
        if (!line.empty()) {
          SequelLiteralUpdateResult result =
              sqliteExecuteLiteralWithDB(db, line);
          if (result.type == SQLiteError) {
            sqliteExecuteLiteralWithDB(db, "ROLLBACK");
            sqFile.close();
            return {SQLiteError, result.message, 0, commands};
          } else {
            affectedRows += result.affectedRows;
            commands++;
          }
        }
      }
      sqFile.close();
      sqliteExecuteLiteralWithDB(db, "COMMIT");
      return {SQLiteOk, "", affectedRows, commands};
    } catch (...) {
      sqFile.close();
      sqliteExecuteLiteralWithDB(db, "ROLLBACK");
      return {SQLiteError,
              "[react-native-quick-sqlite][loadSQLFile] Unexpected error, "
              "transaction was rolledback",
              0, 0};
    }
  } else {
    return {SQLiteError,
            "[react-native-quick-sqlite][loadSQLFile] Could not open file", 0,
            0};
  }
}