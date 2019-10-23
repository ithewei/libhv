#include "sql.h"

// select count(*) from $table_name where $where;
void sql_count(std::string& sql, const char* table_name, const char* where) {
    sql = "select count(*) from ";
    sql += table_name;
    if (where) {
        sql += " where ";
        sql += where;
    }
    sql += ';';
}
// select $keys from $table_name where $where limit $limit order by $column ASC|DESC;
void sql_select(std::string& sql, const char* table_name, const char* keys, const char* where, const KeyVal* options) {
    sql = "select ";
    if (keys) {
        sql += keys;
    }
    else {
        sql += '*';
    }
    sql += " from ";
    sql += table_name;
    if (where) {
        sql += " where ";
        sql += where;
    }
    if (options) {
        for (KeyVal::const_iterator iter = options->begin(); iter != options->end(); ++iter) {
            sql += ' ';
            sql += iter->first;
            sql += ' ';
            sql += iter->second;
        }
    }
    sql += ';';
}
// insert into $table_name ($keys) values ($values);
void sql_insert(std::string& sql, const char* table_name, const char* keys, const char* values) {
    sql = "insert into ";
    sql += table_name;
    if (keys) {
        sql += " (";
        sql += keys;
        sql += ')';
    }
    if (values) {
        sql += " values ";
        sql += '(';
        sql += values;
        sql += ')';
    }
    sql += ';';
}
// replace into $table_name ($keys) values ($values);
void sql_replace(std::string& sql, const char* table_name, const char* keys, const char* values) {
    sql = "replace into ";
    sql += table_name;
    if (keys) {
        sql += " (";
        sql += keys;
        sql += ')';
    }
    if (values) {
        sql += " values ";
        sql += '(';
        sql += values;
        sql += ')';
    }
    sql += ';';
}
// update $table_name set $set where $where;
void sql_update(std::string& sql, const char* table_name, const char* set, const char* where) {
    sql = "update ";
    sql += table_name;
    if (set) {
        sql += " set ";
        sql += set;
    }
    if (where) {
        sql += " where ";
        sql += where;
    }
    sql += ';';
}
// delete from $table_name where $where;
void sql_delete(std::string& sql, const char* table_name, const char* where) {
    sql = "delete from ";
    sql += table_name;
    if (where) {
        sql += " where ";
        sql += where;
    }
    sql += ';';
}
