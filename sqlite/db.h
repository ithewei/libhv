#ifndef SQLITE_DB_H_
#define SQLITE_DB_H_

#include <vector>
#include <string>
#include <map>

#include "sqlite3.h"

#define SQL_OK  SQLITE_OK
#define SQL_ERR SQLITE_ERROR

typedef sqlite3* HDB;
typedef int (*db_callback)(void* userdata, int rows, char** values, char** keys);
typedef std::map<std::string, std::string> KeyVal;
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

// select count(*) from sqlite_master where type='table' and name='$table_name';
int dbtable_exist(HDB hdb, const char* table_name);
// select count(*) from $table_name where $where;
int dbtable_count(HDB hdb, const char* table_name, const char* where);
// select keys from $table_name where $where limit $limit order by $column ASC|DESC;
int dbtable_select(HDB hdb, const char* table_name, const char* keys, const char* where, DBTable* table, const KeyVal* options=NULL);
// insert into $table_name ($keys) values ($values);
int dbtable_insert(HDB hdb, const char* table_name, const char* keys, const char* values);
// update $table_name set $set where $where;
int dbtable_update(HDB hdb, const char* table_name, const char* set, const char* where);
// delete from $table_name where $where;
int dbtable_delete(HDB hdb, const char* table_name, const char* where);

int dbtable_get_index(const char* key, const DBTable& table);

#endif  // SQLITE_DB_H_

