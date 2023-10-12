#include "JSIHelper.h"
#include "sqlite3.h"
#include <map>
#include <string>
#include <vector>

SQLiteOPResult
sqliteExecuteWithDB(sqlite3 *db, std::string const &query,
                    std::vector<QuickValue> *params,
                    std::vector<map<std::string, QuickValue>> *results,
                    std::vector<QuickColumnMetadata> *metadata);

SequelLiteralUpdateResult sqliteExecuteLiteralWithDB(sqlite3 *db,
                                                     std::string const &query);

void bindStatement(sqlite3_stmt *statement, std::vector<QuickValue> *values);