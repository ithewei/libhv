#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSY_TIMEOUT    5000 // ms

int db_open(const char* dbfile, HDB* phdb) {
    if (sqlite3_open(dbfile, phdb) != SQLITE_OK) {
        fprintf(stderr, "sqlite3_open %s failed!\n", dbfile);
        return SQL_ERR;
    }

    sqlite3_busy_timeout(*phdb, BUSY_TIMEOUT);
    return SQL_OK;
}

int db_close(HDB* phdb) {
    if (phdb == NULL || *phdb == NULL) {
        return SQL_ERR;
    }
    int ret = sqlite3_close(*phdb);
    *phdb = NULL;
    return ret;
}

int db_exec(HDB hdb, const char* sql) {
    char *errmsg;
    //printf("sql: %s\n", sql);
    if (sqlite3_exec(hdb, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "sqlite3_exec sql: %s err: %s\n", sql, errmsg);
        return SQL_ERR;
    }
    return SQL_OK;
}

int db_exec_with_result(HDB hdb, const char* sql, DBTable* table) {
    int row, col;
    char **results;
    char *errmsg;
    //printf("sql: %s\n", sql);
    if (sqlite3_get_table(hdb, sql, &results, &row, &col, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "sqlite3_get_table sql: %s err: %s\n", sql, errmsg);
        return SQL_ERR;
    }

    // convert char** to DBTable
    DBRecord record;
    for (int r = 0; r <= row; ++r) {  // note: row[0] is thead
        DBRow tr;
        for (int c = 0; c < col; ++c) {
            tr.push_back(results[r*col + c]);
        }
        table->push_back(tr);
    }
    sqlite3_free_table(results);
    return SQL_OK;
}

int db_exec_cb(HDB hdb, const char* sql, db_callback cb, void* userdata) {
    char *errmsg;
    //printf("sql: %s\n", sql);
    if (sqlite3_exec(hdb, sql, cb, userdata, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "sqlite3_exec sql: %s err: %s\n", sql, errmsg);
        return SQL_ERR;
    }
    return SQL_OK;
}
/////////////////////////////////////////////////////////////////////////////////
// select count(*) from sqlite_master where type='table' and name='$table_name';
int dbtable_exist(HDB hdb, const char* table_name) {
    std::string where;
    where = "type='table' and name=";
    where += '\'';
    where += table_name;
    where += '\'';
    return dbtable_count(hdb, "sqlite_master", where.c_str());
}

int dbtable_count(HDB hdb, const char* table_name, const char* where) {
    std::string sql;
    sql_count(sql, table_name, where);
    DBTable table;
    if (db_exec_with_result(hdb, sql.c_str(), &table) == SQL_OK) {
        return atoi(table[1][0].c_str());
    }
    return 0;
}

int dbtable_select(HDB hdb, const char* table_name, const char* keys, const char* where, DBTable* table, const KeyVal* options) {
    std::string sql;
    sql_select(sql, table_name, keys, where, options);
    return db_exec_with_result(hdb, sql.c_str(), table);
}

int dbtable_insert(HDB hdb, const char* table_name, const char* keys, const char* values) {
    std::string sql;
    sql_insert(sql, table_name, keys, values);
    return db_exec(hdb, sql.c_str());
}

int dbtable_replace(HDB hdb, const char* table_name, const char* keys, const char* values) {
    std::string sql;
    sql_replace(sql, table_name, keys, values);
    return db_exec(hdb, sql.c_str());
}

int dbtable_update(HDB hdb, const char* table_name, const char* set, const char* where) {
    std::string sql;
    sql_update(sql, table_name, set, where);
    return db_exec(hdb, sql.c_str());
}

int dbtable_delete(HDB hdb, const char* table_name, const char* where) {
    std::string sql;
    sql_delete(sql, table_name, where);
    return db_exec(hdb, sql.c_str());
}
////////////////////////////////////////////////////////////////////////////////

int dbtable_get_index(const char* key, const DBTable& table) {
    if (table.size() == 0) {
        return -1;
    }
    const DBRow& thead = table[0];
    for (size_t i = 0; i < thead.size(); ++i) {
        if (strcmp(key, thead[i].c_str()) == 0) {
            return i;
        }
    }
    return -1;
}

