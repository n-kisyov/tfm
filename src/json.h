#ifndef JSON_H
#define JSON_H

#include <wchar.h>
#include <stdint.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_OBJECT,
    JSON_ARRAY
} JsonType;

typedef struct JsonValue {
    JsonType type;
    union {
        int      bool_val;
        double   num_val;
        wchar_t *str_val;
        struct {
            struct JsonPair *pairs;
            int count;
            int cap;
        } obj;
        struct {
            struct JsonValue **items;
            int count;
            int cap;
        } arr;
    };
} JsonValue;

typedef struct JsonPair {
    wchar_t *key;
    JsonValue value;
} JsonPair;

JsonValue  json_parse(const wchar_t *text);
void       json_free(JsonValue *v);
JsonValue *json_get(JsonValue *obj, const wchar_t *key);
JsonValue *json_arr_get(JsonValue *arr, int idx);
const wchar_t *json_get_str(JsonValue *obj, const wchar_t *key, const wchar_t *def);
double    json_get_num(JsonValue *obj, const wchar_t *key, double def);
int       json_get_bool(JsonValue *obj, const wchar_t *key, int def);

#endif
