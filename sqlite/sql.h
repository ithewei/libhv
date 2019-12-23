#ifndef HV_SQL_H_
#define HV_SQL_H_

#include <string>
#include <map>
typedef std::map<std::string, std::string> KeyVal;

// select count(*) from $table_name where $where;
void sql_count(std::string& sql, const char* table_name, const char* where = NULL);
// select $keys from $table_name where $where limit $limit order by $column ASC|DESC;
void sql_select(std::string& sql, const char* table_name, const char* keys = "*", const char* where = NULL, const KeyVal* options = NULL);
// insert into $table_name ($keys) values ($values);
void sql_insert(std::string& sql, const char* table_name, const char* keys, const char* values);
// replace into $table_name ($keys) values ($values);
void sql_replace(std::string& sql, const char* table_name, const char* keys, const char* values);
// update $table_name set $set where $where;
void sql_update(std::string& sql, const char* table_name, const char* set, const char* where = NULL);
// delete from $table_name where $where;
void sql_delete(std::string& sql, const char* table_name, const char* where = NULL);

#endif // HV_SQL_H_
