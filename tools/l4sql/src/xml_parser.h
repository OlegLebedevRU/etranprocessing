#ifndef XML_PARSER_H
#define XML_PARSER_H

#include <windows.h>
#include <stdbool.h>

typedef struct {
    char name[128];
    char server[256];
    char database[128];
    char user[128];
    char password[128];
    int auth_type; // 0 = Windows Authentication (default), 1 = SQL Authentication
    char config_file_path[MAX_PATH];
    char terminal_dir[MAX_PATH];
} DBConfig;

// Initialize config with safe defaults
void db_config_init(DBConfig* cfg);

// Parse DBConfig.xml from file path
bool db_config_parse_file(const wchar_t* xml_file_path, DBConfig* out_cfg);

// Parse DBConfig.xml from memory buffer
bool db_config_parse_string(const char* xml_str, DBConfig* out_cfg);

#endif // XML_PARSER_H
