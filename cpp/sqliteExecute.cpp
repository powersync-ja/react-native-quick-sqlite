#include "sqliteExecute.h"

void bindStatement(sqlite3_stmt *statement, vector<QuickValue> *values) {
  size_t size = values->size();
  if (size <= 0) {
    return;
  }

  for (int ii = 0; ii < size; ii++) {
    int sqIndex = ii + 1;
    QuickValue value = values->at(ii);
    QuickDataType dataType = value.dataType;
    if (dataType == NULL_VALUE) {
      sqlite3_bind_null(statement, sqIndex);
    } else if (dataType == BOOLEAN) {
      sqlite3_bind_int(statement, sqIndex, value.booleanValue);
    } else if (dataType == INTEGER) {
      sqlite3_bind_int(statement, sqIndex, (int)value.doubleOrIntValue);
    } else if (dataType == DOUBLE) {
      sqlite3_bind_double(statement, sqIndex, value.doubleOrIntValue);
    } else if (dataType == INT64) {
      sqlite3_bind_int64(statement, sqIndex, value.int64Value);
    } else if (dataType == TEXT) {
      sqlite3_bind_text(statement, sqIndex, value.textValue.c_str(),
                        value.textValue.length(), SQLITE_TRANSIENT);
    } else if (dataType == ARRAY_BUFFER) {
      sqlite3_bind_blob(statement, sqIndex, value.arrayBufferValue.get(),
                        value.arrayBufferSize, SQLITE_STATIC);
    }
  }
}

SQLiteOPResult
sqliteExecuteWithDB(sqlite3 *db, std::string const &query,
                    std::vector<QuickValue> *params,
                    std::vector<map<std::string, QuickValue>> *results,
                    std::vector<QuickColumnMetadata> *metadata) {
  sqlite3_stmt *statement;

  int statementStatus =
      sqlite3_prepare_v2(db, query.c_str(), -1, &statement, NULL);

  if (statementStatus ==
      SQLITE_OK) // statemnet is correct, bind the passed parameters
  {
    bindStatement(statement, params);
  } else {
    const char *message = sqlite3_errmsg(db);
    return SQLiteOPResult{
        .type = SQLiteError,
        .errorMessage = "[react-native-quick-sqlite] SQL execution error: " +
                        string(message),
        .rowsAffected = 0};
  }

  bool isConsuming = true;
  bool isFailed = false;

  int result, i, count, column_type;
  std::string column_name, column_declared_type;
  std::map<string, QuickValue> row;

  while (isConsuming) {
    result = sqlite3_step(statement);

    switch (result) {
    case SQLITE_ROW:
      if (results == NULL) {
        break;
      }

      i = 0;
      row = std::map<std::string, QuickValue>();
      count = sqlite3_column_count(statement);

      while (i < count) {
        column_type = sqlite3_column_type(statement, i);
        column_name = sqlite3_column_name(statement, i);

        switch (column_type) {

        case SQLITE_INTEGER: {
          /**
           * It's not possible to send a int64_t in a jsi::Value because JS
           * cannot represent the whole number range. Instead, we're sending a
           * double, which can represent all integers up to 53 bits long, which
           * is more than what was there before (a 32-bit int).
           *
           * See https://github.com/margelo/react-native-quick-sqlite/issues/16
           * for more context.
           */
          double column_value = sqlite3_column_double(statement, i);
          row[column_name] = createIntegerQuickValue(column_value);
          break;
        }

        case SQLITE_FLOAT: {
          double column_value = sqlite3_column_double(statement, i);
          row[column_name] = createDoubleQuickValue(column_value);
          break;
        }

        case SQLITE_TEXT: {
          const char *column_value =
              reinterpret_cast<const char *>(sqlite3_column_text(statement, i));
          int byteLen = sqlite3_column_bytes(statement, i);
          // Specify length too; in case string contains NULL in the middle
          // (which SQLite supports!)
          row[column_name] =
              createTextQuickValue(std::string(column_value, byteLen));
          break;
        }

        case SQLITE_BLOB: {
          int blob_size = sqlite3_column_bytes(statement, i);
          const void *blob = sqlite3_column_blob(statement, i);
          uint8_t *data;
          memcpy(data, blob, blob_size);
          row[column_name] = createArrayBufferQuickValue(data, blob_size);
          break;
        }

        case SQLITE_NULL:
          // Intentionally left blank to switch to default case
        default:
          row[column_name] = createNullQuickValue();
          break;
        }
        i++;
      }
      results->push_back(move(row));
      break;
    case SQLITE_DONE:
      if (metadata != NULL) {
        i = 0;
        count = sqlite3_column_count(statement);
        while (i < count) {
          column_name = sqlite3_column_name(statement, i);
          const char *tp = sqlite3_column_decltype(statement, i);
          column_declared_type = tp != NULL ? tp : "UNKNOWN";
          QuickColumnMetadata meta = {
              .colunmName = column_name,
              .columnIndex = i,
              .columnDeclaredType = column_declared_type,
          };
          metadata->push_back(meta);
          i++;
        }
      }
      isConsuming = false;
      break;

    default:
      isFailed = true;
      isConsuming = false;
    }
  }

  sqlite3_finalize(statement);

  if (isFailed) {
    const char *message = sqlite3_errmsg(db);
    return SQLiteOPResult{
        .type = SQLiteError,
        .errorMessage = "[react-native-quick-sqlite] SQL execution error: " +
                        std::string(message),
        .rowsAffected = 0,
        .insertId = 0};
  }

  int changedRowCount = sqlite3_changes(db);
  long long latestInsertRowId = sqlite3_last_insert_rowid(db);
  return SQLiteOPResult{.type = SQLiteOk,
                        .rowsAffected = changedRowCount,
                        .insertId = static_cast<double>(latestInsertRowId)};
}

SequelLiteralUpdateResult sqliteExecuteLiteralWithDB(sqlite3 *db,
                                                     string const &query) {
  // SQLite statements need to be compiled before executed
  sqlite3_stmt *statement;

  // Compile and move result into statement memory spot
  int statementStatus =
      sqlite3_prepare_v2(db, query.c_str(), -1, &statement, NULL);

  if (statementStatus !=
      SQLITE_OK) // statemnet is correct, bind the passed parameters
  {
    const char *message = sqlite3_errmsg(db);
    return {SQLiteError,
            "[react-native-quick-sqlite] SQL execution error: " +
                string(message),
            0};
  }

  bool isConsuming = true;
  bool isFailed = false;

  int result, i, count, column_type;
  string column_name;

  while (isConsuming) {
    result = sqlite3_step(statement);

    switch (result) {
    case SQLITE_ROW:
      isConsuming = true;
      break;

    case SQLITE_DONE:
      isConsuming = false;
      break;

    default:
      isFailed = true;
      isConsuming = false;
    }
  }

  sqlite3_finalize(statement);

  if (isFailed) {
    const char *message = sqlite3_errmsg(db);
    return {SQLiteError,
            "[react-native-quick-sqlite] SQL execution error: " +
                string(message),
            0};
  }

  int changedRowCount = sqlite3_changes(db);
  return {SQLiteOk, "", changedRowCount};
}
