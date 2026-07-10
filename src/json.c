#include "json.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    const wchar_t *text;
    int pos;
    int len;
} Parser;

static void json_value_free(JsonValue *v);

static wchar_t peek(Parser *p) {
    return p->pos < p->len ? p->text[p->pos] : 0;
}

static wchar_t next(Parser *p) {
    return p->pos < p->len ? p->text[p->pos++] : 0;
}

static void skip_ws(Parser *p) {
    while (p->pos < p->len) {
        wchar_t c = peek(p);
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') {
            next(p);
        } else {
            break;
        }
    }
}

static JsonValue parse_value(Parser *p);

static wchar_t *parse_string(Parser *p) {
    if (next(p) != L'"') return NULL;
    wchar_t buf[4096];
    int bi = 0;
    while (p->pos < p->len && bi < 4095) {
        wchar_t c = next(p);
        if (c == L'"') {
            buf[bi] = 0;
            return wcsdup_safe(buf);
        }
        if (c == L'\\') {
            wchar_t ec = next(p);
            switch (ec) {
                case L'"':  buf[bi++] = L'"'; break;
                case L'\\': buf[bi++] = L'\\'; break;
                case L'/':  buf[bi++] = L'/'; break;
                case L'b':  buf[bi++] = L'\b'; break;
                case L'f':  buf[bi++] = L'\f'; break;
                case L'n':  buf[bi++] = L'\n'; break;
                case L'r':  buf[bi++] = L'\r'; break;
                case L't':  buf[bi++] = L'\t'; break;
                case L'u': {
                    wchar_t hex[5] = {0};
                    for (int i = 0; i < 4; i++) hex[i] = next(p);
                    buf[bi++] = (wchar_t)wcstol(hex, NULL, 16);
                    break;
                }
                default: buf[bi++] = ec; break;
            }
        } else {
            buf[bi++] = c;
        }
    }
    buf[bi] = 0;
    return wcsdup_safe(buf);
}

static JsonValue parse_object(Parser *p) {
    JsonValue v;
    v.type = JSON_OBJECT;
    v.obj.pairs = NULL;
    v.obj.count = 0;
    v.obj.cap = 0;
    next(p); /* skip { */
    skip_ws(p);
    if (peek(p) == L'}') { next(p); return v; }
    while (1) {
        skip_ws(p);
        wchar_t *key = parse_string(p);
        if (!key) break;
        skip_ws(p);
        if (next(p) != L':') { free(key); break; }
        skip_ws(p);
        JsonValue val = parse_value(p);
        int idx = v.obj.count++;
        if (v.obj.count > v.obj.cap) {
            v.obj.cap = v.obj.cap ? v.obj.cap * 2 : 4;
            v.obj.pairs = (JsonPair *)xrealloc(v.obj.pairs, v.obj.cap * sizeof(JsonPair));
        }
        v.obj.pairs[idx].key = key;
        v.obj.pairs[idx].value = val;
        skip_ws(p);
        wchar_t c = next(p);
        if (c == L'}') break;
        if (c != L',') break;
    }
    return v;
}

static JsonValue parse_array(Parser *p) {
    JsonValue v;
    v.type = JSON_ARRAY;
    v.arr.items = NULL;
    v.arr.count = 0;
    v.arr.cap = 0;
    next(p); /* skip [ */
    skip_ws(p);
    if (peek(p) == L']') { next(p); return v; }
    while (1) {
        skip_ws(p);
        JsonValue item = parse_value(p);
        int idx = v.arr.count++;
        if (v.arr.count > v.arr.cap) {
            v.arr.cap = v.arr.cap ? v.arr.cap * 2 : 4;
            v.arr.items = (JsonValue **)xrealloc(v.arr.items, v.arr.cap * sizeof(JsonValue *));
        }
        v.arr.items[idx] = (JsonValue *)malloc(sizeof(JsonValue));
        if (v.arr.items[idx]) *v.arr.items[idx] = item;
        skip_ws(p);
        wchar_t c = next(p);
        if (c == L']') break;
        if (c != L',') break;
    }
    return v;
}

static JsonValue parse_value(Parser *p) {
    JsonValue v = {0};
    skip_ws(p);
    wchar_t c = peek(p);
    if (c == L'{') {
        return parse_object(p);
    } else if (c == L'[') {
        return parse_array(p);
    } else if (c == L'"') {
        wchar_t *s = parse_string(p);
        v.type = JSON_STRING;
        v.str_val = s;
        return v;
    } else if (c == L't' || c == L'f') {
        v.type = JSON_BOOL;
        if (wcsncmp(p->text + p->pos, L"true", 4) == 0) {
            v.bool_val = 1; p->pos += 4;
        } else if (wcsncmp(p->text + p->pos, L"false", 5) == 0) {
            v.bool_val = 0; p->pos += 5;
        }
        return v;
    } else if (c == L'n') {
        if (wcsncmp(p->text + p->pos, L"null", 4) == 0) {
            v.type = JSON_NULL; p->pos += 4;
        }
        return v;
    } else if ((c >= L'0' && c <= L'9') || c == L'-' || c == L'+' || c == L'.') {
        wchar_t *end;
        v.type = JSON_NUMBER;
        v.num_val = wcstod(p->text + p->pos, &end);
        p->pos = (int)(end - p->text);
        return v;
    }
    return v;
}

JsonValue json_parse(const wchar_t *text) {
    Parser p = {text, 0, (int)wcslen(text)};
    return parse_value(&p);
}

static void json_value_free(JsonValue *v) {
    if (!v) return;
    switch (v->type) {
    case JSON_STRING:
        free(v->str_val);
        break;
    case JSON_OBJECT:
        for (int i = 0; i < v->obj.count; i++) {
            free(v->obj.pairs[i].key);
            json_value_free(&v->obj.pairs[i].value);
        }
        free(v->obj.pairs);
        break;
    case JSON_ARRAY:
        for (int i = 0; i < v->arr.count; i++) {
            json_value_free(v->arr.items[i]);
            free(v->arr.items[i]);
        }
        free(v->arr.items);
        break;
    default:
        break;
    }
    memset(v, 0, sizeof(JsonValue));
}

void json_free(JsonValue *v) {
    json_value_free(v);
}

JsonValue *json_get(JsonValue *obj, const wchar_t *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (int i = 0; i < obj->obj.count; i++) {
        if (wcscmp(obj->obj.pairs[i].key, key) == 0)
            return &obj->obj.pairs[i].value;
    }
    return NULL;
}

JsonValue *json_arr_get(JsonValue *arr, int idx) {
    if (!arr || arr->type != JSON_ARRAY || idx < 0 || idx >= arr->arr.count)
        return NULL;
    return arr->arr.items[idx];
}

const wchar_t *json_get_str(JsonValue *obj, const wchar_t *key, const wchar_t *def) {
    JsonValue *v = json_get(obj, key);
    if (v && v->type == JSON_STRING) return v->str_val;
    return def;
}

double json_get_num(JsonValue *obj, const wchar_t *key, double def) {
    JsonValue *v = json_get(obj, key);
    if (v && v->type == JSON_NUMBER) return v->num_val;
    return def;
}

int json_get_bool(JsonValue *obj, const wchar_t *key, int def) {
    JsonValue *v = json_get(obj, key);
    if (v && v->type == JSON_BOOL) return v->bool_val;
    return def;
}
