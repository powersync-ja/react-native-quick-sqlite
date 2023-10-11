/*
 * sequel.h
 *
 * Created by Oscar Franco on 2021/03/07
 * Copyright (c) 2021 Oscar Franco
 *
 * This code is licensed under the MIT license
 */

#include "JSIHelper.h"
#include <sqlite3.h>
#include <vector>

using namespace std;
using namespace facebook;

SQLiteOPResult sqliteOpenDb(string const dbName, string const docPath,
                            sqlite3 **db);
SQLiteOPResult genericSqliteOpenDb(string const dbName, string const docPath,
                                   sqlite3 **db, int sqlOpenFlags);

SQLiteOPResult sqliteCloseDb(string const dbName);

SQLiteOPResult sqliteRemoveDb(string const dbName, string const docPath);

SQLiteOPResult sqliteAttachDb(string const mainDBName, string const docPath,
                              string const databaseToAttach,
                              string const alias);

SQLiteOPResult sqliteDetachDb(string const mainDBName, string const alias);

SQLiteOPResult sqliteExecute(string const dbName, string const &query,
                             vector<QuickValue> *values,
                             vector<map<string, QuickValue>> *result,
                             vector<QuickColumnMetadata> *metadata);

SQLiteOPResult sqliteExecuteWithDB(sqlite3 *db, string const &query,
                                   vector<QuickValue> *params,
                                   vector<map<string, QuickValue>> *results,
                                   vector<QuickColumnMetadata> *metadata);

SequelLiteralUpdateResult sqliteExecuteLiteral(string const dbName,
                                               string const &query);
SequelLiteralUpdateResult sqliteExecuteLiteralWithDB(sqlite3 *db,
                                                     string const &query);

char *getDBName(sqlite3 *db);

void sqliteCloseAll();
