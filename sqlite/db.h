#ifndef SQLITE_DB_H_
#define SQLITE_DB_H_

#include <vector>

#include "sqlite3.h"

#include "sql.h"

#define SQL_OK  SQLITE_OK
#define SQL_ERR SQLITE_ERROR

typedef sqlite3* HDB;
typedef int (*db_callback)(void* userdata, int rows, char** values, char** keys);
typedef KeyVal DBRecord;
typedef KeyVal DBOption;
typedef std::vector<std::string> DBRow;
typedef std::vector<std::string> DBColomn;
typedef std::vector<DBRow> DBTable;

int db_open(const char* dbfile, HDB* phdb);
int db_close(HDB* phdb);

int db_exec(HDB hdb, const char* sql);
int db_exec_with_result(HDB hdb, const char* sql, DBTable* table);
int db_exec_cb(HDB hdb, const char* sql, db_callback cb, void* userdata);

int dbtable_exist(HDB hdb, const char* table_name);
int dbtable_count(HDB hdb, const char* table_name, const char* where);
int dbtable_select(HDB hdb, const char* table_name, const char* keys, const char* where, DBTable* table, const KeyVal* options=NULL);
int dbtable_insert(HDB hdb, const char* table_name, const char* keys, const char* values);
int dbtable_replace(HDB hdb, const char* table_name, const char* keys, const char* values);
int dbtable_update(HDB hdb, const char* table_name, const char* set, const char* where);
int dbtable_delete(HDB hdb, const char* table_name, const char* where);

int dbtable_get_index(const char* key, const DBTable& table);

#endif  // SQLITE_DB_H_
