#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Codegen.h"
#include "Semantics.h"
#include "Error.h"
#include "regmap.h"

#define MAX_SPILL_SLOTS 64

GlobalArrayInit* global_array_inits = NULL;

bool has_print = false;
bool has_string_lib = false;
bool has_memory_lib = false;
bool has_float_lib = false;
bool in_main = false;
bool has_array_lib = false;
bool has_bounds_check = false;
bool has_format = false;
bool has_input = false;
bool has_system_lib = false;
bool has_gc = false;
bool has_unsafe_lib = false;
bool has_64bit_ops = false;
bool has_exception_lib = false;
bool has_file_lib = false;
bool has_socket_lib = false;
bool has_window_lib = false;
bool has_image_lib = false;
bool has_music_lib = false;
bool has_video_lib = false;
bool has_time_lib = false;
bool has_thread_lib = false;
bool has_sync_lib = false;
bool has_function_lib = false;
bool has_sqlite = false;

static char* file_extern_names[64];
static int file_extern_count = 0;
char* current_function = NULL;
int current_param_offset = 8;
int current_param_index = 0;
bool in_function = false;
int current_local_offset = 0;
SpillSlot spill_slots[MAX_SPILL_SLOTS];
int spill_count = 0;
static int arrinit_i = 0;

int const_i = 0;
int buffer_i = 0;
Const* consts = NULL;

static int pending_vararg_idx = 0;
static int pending_vararg_count = 0;
static char** pending_vararg_names = NULL;
static int* pending_vararg_bytes_array = NULL;
static int cg64_label_counter = 0;
static int uf_clamp_label = 0;
static int spill_base = 0;
static IR* current_func_end = NULL;
static bool cg_func_has_hidden_ret = false;

static const char* canonical_name(const char* name);

typedef struct {
    bool in_use;
    char* var_name;
    Reg reg;
    bool dirty;
    bool has_addr;
    char addr[96];
} CacheSlot;

static CacheSlot cache_slots[5];
static RegMap* current_regmap = NULL;
static const char* CG_REG_NAMES[] = { "eax", "ecx", "edx", "ebx", "esi", "edi" };
static char* strip_ssa_suffix(const char* name);
static char** escaped_vars = NULL;
static int escaped_var_count = 0;
static int escaped_var_capacity = 0;

static IR* find_func_last_ret(IR* label_ir) {
    IR* last_ret = NULL;
    for (IR* p = label_ir; p; p = p->next) {
        if (p != label_ir && p->type == IR_LABEL && p->has_return)
            break;
        if (p->type == IR_RET)
            last_ret = p;
    }
    return last_ret;
}

static void escape_set_reset(void) {
    for (int i = 0; i < escaped_var_count; i++)
        free(escaped_vars[i]);
    escaped_var_count = 0;
}

static void escape_set_add(const char* base) {
    if (!base) return;
    for (int i = 0; i < escaped_var_count; i++)
        if (strcmp(escaped_vars[i], base) == 0)
            return;
    if (escaped_var_count >= escaped_var_capacity) {
        escaped_var_capacity = escaped_var_capacity ? escaped_var_capacity * 2 : 16;
        escaped_vars = realloc(escaped_vars,
                               sizeof(char*) * (size_t)escaped_var_capacity);
        if (!escaped_vars) OUT_OF_MEMORY();
    }
    escaped_vars[escaped_var_count++] = strdup(base);
}

static bool var_escapes(const char* name) {
    if (!name) return true;
    char* base = strip_ssa_suffix(name);
    if (!base) return true;
    for (int i = 0; i < escaped_var_count; i++) {
        if (strcmp(escaped_vars[i], base) == 0) {
            free(base);
            return true;
        }
    }
    free(base);
    return false;
}

static void scan_function_escapes(IR* label_ir) {
    escape_set_reset();
    if (!label_ir) return;
    IR* scan = label_ir->next;
    while (scan) {
        if (scan->type == IR_LABEL && scan->has_return)
            break;
        const char* victim = NULL;
        if (scan->type == IR_ADDRESS_OF && !scan->is_function_addr) {
            victim = scan->args1;
        } else if (scan->type == IR_PUSH && scan->push_bytes > 4
                   && !scan->is_str_param) {
            victim = scan->args1;
        }
        if (victim && strncmp(victim, "_var", 4) == 0) {
            char* base = strip_ssa_suffix(victim);
            escape_set_add(base);
            free(base);
        }
        scan = scan->next;
    }
}

static int cache_find(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < 5; i++) {
        if (cache_slots[i].in_use && cache_slots[i].var_name
            && strcmp(cache_slots[i].var_name, name) == 0)
            return i;
    }
    return -1;
}

static void cache_drop_slot(int i) {
    free(cache_slots[i].var_name);
    cache_slots[i].var_name = NULL;
    cache_slots[i].in_use = false;
    cache_slots[i].dirty = false;
    cache_slots[i].has_addr = false;
}

static void cache_flush_slot(FILE* out, int i) {
    if (!cache_slots[i].in_use) return;
    if (cache_slots[i].dirty && cache_slots[i].has_addr) {
        fprintf(out, "  mov dword %s, %s\n",
                cache_slots[i].addr, CG_REG_NAMES[cache_slots[i].reg]);
    }
    cache_drop_slot(i);
}

static void cache_flush_all(FILE* out) {
    for (int i = 0; i < 5; i++)
        cache_flush_slot(out, i);
}

static void cache_flush_reg(FILE* out, Reg reg) {
    if (reg < R_ECX || reg > R_EDI) return;
    cache_flush_slot(out, reg - R_ECX);
}

static void cache_invalidate(FILE* out, const char* name) {
    int i = cache_find(name);
    if (i >= 0)
        cache_flush_slot(out, i);
}

static void cache_install(int slot, const char* name, Reg reg, const char* addr) {
    cache_slots[slot].var_name = strdup(name);
    cache_slots[slot].reg = reg;
    cache_slots[slot].in_use = true;
    cache_slots[slot].dirty = false;
    cache_slots[slot].has_addr = (addr && addr[0] == '[');
    if (cache_slots[slot].has_addr) {
        strncpy(cache_slots[slot].addr, addr, 95);
        cache_slots[slot].addr[95] = '\0';
    }
}

static const RegMapEntry* regmap_find_resident(const char* name) {
    if (!current_regmap || !name) return NULL;
    return regmap_get(current_regmap, name);
}

static bool resident_addr(const char* name, char* addr, int len) {
    if (!name || !addr || len <= 0) return false;
    int idx = get_const((char*)name);
    if (idx == -1) return false;
    if (consts->storage[idx] != STORAGE_LOCAL) return false;
    snprintf(addr, (size_t)len, "[ebp-%d]", -consts->offset[idx]);
    return true;
}

int alloc_spill_slot(void) {
    if (spill_count >= MAX_SPILL_SLOTS) {
        INTERNAL_ERROR("too many spill slots");
    }
    int slot = -4 - spill_count * 4;
    spill_slots[spill_count++] = (SpillSlot){.stack_slot = slot, .is_spilled = true};
    return slot;
}

static char* strip_ssa_suffix(const char* name) {
    if (!name) return NULL;
    const char* at = strrchr(name, '@');
    if (!at || at == name) return strdup(name);
    for (const char* p = at + 1; *p; p++)
        if (!isdigit((unsigned char)*p))
            return strdup(name);
    size_t len = at - name;
    char* buf = malloc(len + 1);
    if (!buf) OUT_OF_MEMORY();
    memcpy(buf, name, len);
    buf[len] = '\0';
    return buf;
}

static char* escape_for_nasm(const char* str) {
    if (str == NULL) return strdup("");

    size_t alloc = 1;
    for (const char* p = str; *p; p++) {
        if ((unsigned char)*p < 32 || *p == '`')
            alloc += 8;
        else if (*p == '\\' && *(p+1) == 'n') {
            alloc += 8; p++;
        } else if (*p == '\\' && *(p+1) == '"') {
            alloc += 8; p++;
        } else
            alloc += 1;
    }
    alloc += 16;

    char* buf = malloc(alloc);
    if (!buf) return strdup(str);
    char* out = buf;

    for (const char* p = str; *p; p++) {
        if (*p == '\\' && *(p+1) == 'n') {
            out += sprintf(out, "`,10,`");
            p++;
        } else if (*p == '\\' && *(p+1) == '"') {
            out += sprintf(out, "`,34,`");
            p++;
        } else if ((unsigned char)*p >= 32 && *p != '`') {
            *out++ = *p;
        } else if (*p == '`') {
            *out++ = '`'; *out++ = '`';
        } else {
            out += sprintf(out, "`,%d,`", (unsigned char)*p);
        }
    }
    *out = '\0';
    return buf;
}

static bool is_local_label(const char* name) {
    if (!name || !name[0]) return false;
    return (name[0] == 'l' && isdigit((unsigned char)name[1]));
}

char* string_buffer_get()
{
    int len = snprintf(NULL, 0, "buffer%d", temp_i);
    char* temp = malloc(len + 1);
    if (temp) {
        snprintf(temp, len + 1, "buffer%d", ++temp_i);
    }
    return temp;
}

bool is_var_name(char* value)
{
    struct key_token token = find_key(value);
    return token.type == TOKEN_IDENTIFIER;
}

bool is_temp(const char* str)
{
    if (str == NULL) return false;
    return str[0] == '.' && str[1] == 't' && isdigit((unsigned char)str[2]);
}

bool is_var(const char* str)
{
    if (str == NULL) return false;
    return str[0] == '_' && str[1] == 'v' && str[2] == 'a' && str[3] == 'r';
}

bool is_number(char* code)
{
    if (code == NULL || code[0] == '\0') return false;
    char* s = code;
    if (*s == '-') s++;
    if (*s == '\0') return false;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        if (!isxdigit((unsigned char)*s)) return false;
        while (isxdigit((unsigned char)*s)) s++;
        return *s == '\0';
    }

    if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        s += 2;
        while (*s == '0' || *s == '1') s++;
        return *s == '\0';
    }

    if (s[0] == '0' && s[1] != '\0' && s[1] != 'x' && s[1] != 'X'
        && s[1] != 'b' && s[1] != 'B' && s[1] != '.') {
        s++;
        while (*s >= '0' && *s <= '7') s++;
        if (*s == '\0') return true;
        return false;
        }

    bool has_digit = false;
    bool has_dot   = false;
    while (*s != '\0') {
        if (isdigit((unsigned char)*s)) {
            has_digit = true;
        } else if (*s == '.' && !has_dot) {
            has_dot = true;
        } else {
            return false;
        }
        s++;
    }
    return has_digit;
}

static int get_type_size(DataType type) {
    switch (type) {
    case DATA_CHAR:
    case DATA_BOOL:   return 1;
    case DATA_INT8:   case DATA_UINT8:   return 1;
    case DATA_INT16:  case DATA_UINT16:  return 2;
    case DATA_INT:    case DATA_UINT:
    case DATA_INT32:  case DATA_UINT32:
    case DATA_FLOAT:  case DATA_UFLOAT:
    case DATA_FLOAT32: case DATA_UFLOAT32:
    case DATA_STRING: return 4;
    case DATA_INT64:  case DATA_UINT64:
    case DATA_FLOAT64: case DATA_UFLOAT64:
    case DATA_STR:    return 8;
    case DATA_FLOAT8:  case DATA_UFLOAT8:  return 1;
    case DATA_FLOAT16: case DATA_UFLOAT16: return 2;
    case DATA_INT128:  case DATA_UINT128:
    case DATA_FLOAT128: case DATA_UFLOAT128: return 16;
    default:          return 4;
    }
}

static DataType normalize_type(DataType dt) {
    switch (dt) {
    case DATA_INT8: case DATA_INT16: case DATA_INT32: case DATA_INT64: case DATA_INT128:
        return DATA_INT;
    case DATA_UINT8: case DATA_UINT16: case DATA_UINT32: case DATA_UINT64: case DATA_UINT128:
        return DATA_UINT;
    case DATA_FLOAT8: case DATA_FLOAT16: case DATA_FLOAT32: case DATA_FLOAT64: case DATA_FLOAT128:
        return DATA_FLOAT;
    case DATA_UFLOAT8: case DATA_UFLOAT16: case DATA_UFLOAT32: case DATA_UFLOAT64: case DATA_UFLOAT128:
        return DATA_UFLOAT;
    default:
        return dt;
    }
}

char* add_const(DataType type, char* value, int array_size,DataType value_type);

DataType load_var(FILE* out, const char* var_name) {
    if (is_temp(var_name)) {
        int off = temp_map_get_offset(var_name);
        fprintf(out, "  mov eax, dword [ebp-%d]\n", off);
        return DATA_INT;
    }
    if (is_number((char*)var_name)) {
        if (strchr(var_name, '.') != NULL) {
            char* label = add_const(DATA_FLOAT, (char*)var_name, 0, DATA_FLOAT);
            fprintf(out, "  fld dword [%s]\n", label);
            return DATA_FLOAT;
        }
        fprintf(out, "  mov eax, %s\n", var_name);
        return DATA_INT;
    }
    const char* canon = canonical_name(var_name);
    int idx = get_const((char*)canon);
    if (idx == -1) {
        fprintf(out, "  mov eax, %s\n", var_name);
        return DATA_INT;
    }
    DataType type = consts->type[idx];
    char addr[100];

    switch (consts->storage[idx]) {
    case STORAGE_GLOBAL: sprintf(addr, "[%s]", canon); break;
    case STORAGE_PARAM:  sprintf(addr, "[ebp+%d]", consts->offset[idx]); break;
    case STORAGE_LOCAL:  sprintf(addr, "[ebp-%d]", -consts->offset[idx]); break;
    }

    if (consts->is_dynamic_array[idx]) {
        fprintf(out, "  mov eax, dword %s\n", addr);
        return DATA_INT;
    }

    if (consts->is_array[idx] && type != DATA_STR
        && type != DATA_INT64 && type != DATA_UINT64
        && type != DATA_FLOAT64 && type != DATA_UFLOAT64) {
        fprintf(out, "  lea eax, %s\n", addr);
        return DATA_INT;
    }

    if (normalize_type(type) == DATA_FLOAT || normalize_type(type) == DATA_UFLOAT) {
        fprintf(out, "  fld dword %s\n", addr);
    } else if (type == DATA_CHAR || type == DATA_BOOL) {
        fprintf(out, "  movzx eax, byte %s\n", addr);
    } else {
        fprintf(out, "  mov eax, dword %s\n", addr);
    }
    return type;
}

void temp_map_init() {
    temp_map.capacity = 16;
    temp_map.count = 0;
    temp_map.names = malloc(temp_map.capacity * sizeof(char*));
    temp_map.types = malloc(temp_map.capacity * sizeof(DataType));
    temp_map.offsets = malloc(temp_map.capacity * sizeof(int));
    temp_map.is_dynamic_arrays = calloc(temp_map.capacity, sizeof(bool));
    temp_map.array_sizes = calloc(temp_map.capacity, sizeof(int));
    if (!temp_map.names || !temp_map.types || !temp_map.offsets
        || !temp_map.is_dynamic_arrays || !temp_map.array_sizes) {
        OUT_OF_MEMORY();
    }
}

DataType temp_map_get(char* name) {
    if (name == NULL) return DATA_ERROR;

    for (int i = 0; i < temp_map.count; i++) {
        if (strcmp(temp_map.names[i], name) == 0) {
            return temp_map.types[i];
        }
    }

    for (int i = 0; i < consts->index; i++) {
        if (strcmp(consts->name[i], name) == 0) {
            return consts->type[i];
        }
    }

    return DATA_ERROR;
}

void temp_map_cleanup() {
    for (int i = 0; i < temp_map.count; i++) {
        free(temp_map.names[i]);
    }
    free(temp_map.names);
    free(temp_map.types);
    free(temp_map.offsets);
    free(temp_map.is_dynamic_arrays);
    free(temp_map.array_sizes);
    temp_map.names = NULL;
    temp_map.types = NULL;
    temp_map.offsets = NULL;
    temp_map.is_dynamic_arrays = NULL;
    temp_map.array_sizes = NULL;
    temp_map.count = 0;
    temp_map.capacity = 0;
}

void temp_map_ensure_capacity() {
    if (temp_map.count >= temp_map.capacity) {
        temp_map.capacity *= 2;
        temp_map.names = realloc(temp_map.names, temp_map.capacity * sizeof(char*));
        temp_map.types = realloc(temp_map.types, temp_map.capacity * sizeof(DataType));
        temp_map.offsets = realloc(temp_map.offsets, temp_map.capacity * sizeof(int));
        temp_map.is_dynamic_arrays = realloc(temp_map.is_dynamic_arrays, temp_map.capacity * sizeof(bool));
        temp_map.array_sizes = realloc(temp_map.array_sizes, temp_map.capacity * sizeof(int));
        if (!temp_map.names || !temp_map.types || !temp_map.offsets
            || !temp_map.is_dynamic_arrays || !temp_map.array_sizes) {
            OUT_OF_MEMORY();
        }
    }
}

void temp_map_add(char* name, DataType type) {
    if (name == NULL) return;
    for (int i = 0; i < temp_map.count; i++) {
        if (strcmp(temp_map.names[i], name) == 0) {
            if (get_type_size(type) > get_type_size(temp_map.types[i])) {
                temp_map.types[i] = type;
            }
            return;
        }
    }
    temp_map_ensure_capacity();
    temp_map.names[temp_map.count] = strdup(name);
    temp_map.types[temp_map.count] = type;
    temp_map.is_dynamic_arrays[temp_map.count] = false;
    temp_map.array_sizes[temp_map.count] = 0;
    temp_map.count++;
}

void temp_map_mark_dynamic(char* name) {
    if (name == NULL) return;
    for (int i = 0; i < temp_map.count; i++) {
        if (strcmp(temp_map.names[i], name) == 0) {
            temp_map.is_dynamic_arrays[i] = true;
            return;
        }
    }
}

bool temp_map_is_dynamic(const char* name) {
    if (name == NULL) return false;
    for (int i = 0; i < temp_map.count; i++) {
        if (strcmp(temp_map.names[i], name) == 0) {
            return temp_map.is_dynamic_arrays[i];
        }
    }
    return false;
}

void temp_map_set_array_size(const char* name, int size) {
    if (name == NULL) return;
    for (int i = 0; i < temp_map.count; i++) {
        if (strcmp(temp_map.names[i], name) == 0) {
            temp_map.array_sizes[i] = size;
            return;
        }
    }
}

int temp_map_get_array_size(const char* name) {
    if (name == NULL) return 0;
    for (int i = 0; i < temp_map.count; i++) {
        if (strcmp(temp_map.names[i], name) == 0) {
            return temp_map.array_sizes[i];
        }
    }
    return 0;
}

void temp_map_compute_offsets() {
    int current_offset = 16;
    for (int i = 0; i < temp_map.count; i++) {
        int sz = get_type_size(temp_map.types[i]);
        if (sz < 4) sz = 4;
        if (sz >= 8) {
            current_offset += 4;
        }
        temp_map.offsets[i] = current_offset;
        current_offset += sz;
    }
}

int temp_map_get_offset(const char* name) {
    if (name == NULL) return 0;
    for (int i = 0; i < temp_map.count; i++) {
        if (strcmp(temp_map.names[i], name) == 0) {
            return temp_map.offsets[i];
        }
    }
    int idx = atoi(name + 2);
    return 16 + idx * 4;
}

void const_resize()
{
    if (consts->index >= consts->size)
    {
        int old_size = consts->size;
        consts->size *= 2;
        consts->type = realloc(consts->type,sizeof(DataType) * consts->size);
        if (consts->type == NULL) { OUT_OF_MEMORY(); }
        consts->value = realloc(consts->value, sizeof(char*) * consts->size);
        if (consts->value == NULL) { OUT_OF_MEMORY(); }
        consts->name = realloc(consts->name, sizeof(char*) * consts->size);
        if (consts->name == NULL) { OUT_OF_MEMORY(); }
        consts->var_type = realloc(consts->var_type, sizeof(Var_Type) * consts->size);
        if (consts->var_type == NULL) { OUT_OF_MEMORY(); }
        consts->value_type = realloc(consts->value_type, sizeof(DataType) * consts->size);
        if (consts->value_type == NULL) { OUT_OF_MEMORY(); }
        consts->storage = realloc(consts->storage, sizeof(StorageClass) * consts->size);
        if (consts->storage == NULL) { OUT_OF_MEMORY(); }
        consts->offset = realloc(consts->offset, sizeof(int) * consts->size);
        if (consts->offset == NULL) { OUT_OF_MEMORY(); }
        consts->is_array = realloc(consts->is_array, sizeof(bool) * consts->size);
        if (consts->is_array == NULL) { OUT_OF_MEMORY(); }
        consts->array_size = realloc(consts->array_size, sizeof(int) * consts->size);
        if (consts->array_size == NULL) { OUT_OF_MEMORY(); }
        consts->is_dynamic_array = realloc(consts->is_dynamic_array, sizeof(bool) * consts->size);
        if (consts->is_dynamic_array == NULL) { OUT_OF_MEMORY(); }
        for (int i = old_size; i < consts->size; i++) {
            consts->type[i] = DATA_INT;
            consts->value[i] = NULL;
            consts->name[i] = NULL;
            consts->var_type[i] = TYPE_VAR;
            consts->value_type[i] = DATA_INT;
            consts->storage[i] = STORAGE_LOCAL;
            consts->offset[i] = 0;
            consts->is_array[i] = false;
            consts->array_size[i] = 0;
            consts->is_dynamic_array[i] = false;
        }
    }
}

void Manual_add_const(DataType type, char* value, int array_size, char* name, Var_Type var_type,DataType value_type)
{
    const_resize();

    consts->type[consts->index] = type;
    consts->value[consts->index] = strdup(value);
    consts->var_type[consts->index] = var_type;
    consts->name[consts->index] = strdup(strip_ssa_suffix(name));
    consts->value_type[consts->index] = value_type;
    consts->storage[consts->index] = STORAGE_LOCAL;
    consts->offset[consts->index] = 0;
    consts->is_array[consts->index] = false;
    consts->array_size[consts->index] = 0;
    consts->is_dynamic_array[consts->index] = false;
    consts->index++;
}

char* add_const(DataType type, char* value, int array_size,DataType value_type)
{
    if (value != NULL && (type == DATA_FLOAT || type == DATA_INT || type == DATA_INT64 || type == DATA_UINT64)) {
        for (int i = 0; i < consts->index; i++) {
            if (consts->type[i] == type &&
                consts->var_type[i] == TYPE_CONST &&
                consts->value[i] != NULL &&
                strcmp(consts->value[i], value) == 0) {
                return consts->name[i];
            }
        }
    }

    if (type == DATA_STRING && value != NULL) {
        for (int i = 0; i < consts->index; i++) {
            if (consts->type[i] == DATA_STRING &&
                consts->var_type[i] == TYPE_CONST &&
                consts->value[i] != NULL &&
                strcmp(consts->value[i], value) == 0) {
                return consts->name[i];
                }
        }
    }

    const_resize();
    consts->storage[consts->index] = STORAGE_GLOBAL;
    consts->offset[consts->index] = 0;

    char* const_name = NULL;

    switch(type)
    {
    case DATA_STRING:
        {
            int str_len = snprintf(NULL, 0, "_str%d", const_i);
            const_name = malloc(str_len + 1);
            if (const_name) {
                snprintf(const_name, str_len + 1, "_str%d", const_i++);
            }
            consts->type[consts->index] = type;
            consts->value[consts->index] = strdup(value);
            consts->var_type[consts->index] = TYPE_CONST;
            consts->value_type[consts->index] = value_type;
            consts->name[consts->index++] = strdup(const_name);
        }
        break;

    case DATA_FLOAT:
        {
            int len = snprintf(NULL, 0, "_float%d", const_i);
            const_name = malloc(len + 1);
            if (const_name) {
                snprintf(const_name, len + 1, "_float%d", const_i++);
            }
            consts->type[consts->index] = type;
            char* normalized = strdup(value);
            if (normalized[0] == '.') {
                char* tmp = malloc(strlen(normalized) + 3);
                sprintf(tmp, "0%s", normalized);
                free(normalized);
                normalized = tmp;
            }
            size_t Flen = strlen(normalized);
            if (Flen > 0 && normalized[Flen - 1] == '.') {
                char* tmp = malloc(Flen + 3);
                sprintf(tmp, "%s0", normalized);
                free(normalized);
                normalized = tmp;
            }
            bool has_dot = false;
            bool has_exp = false;
            for (const char* p = normalized; *p; p++) {
                if (*p == '.') has_dot = true;
                if (*p == 'e' || *p == 'E') has_exp = true;
            }
            if (!has_dot && !has_exp) {
                char* tmp = malloc(strlen(normalized) + 3);
                sprintf(tmp, "%s.0", normalized);
                free(normalized);
                normalized = tmp;
            }
            consts->value[consts->index] = normalized ? normalized : strdup(value);
            consts->var_type[consts->index] = TYPE_CONST;
            consts->value_type[consts->index] = value_type;
            consts->name[consts->index] = const_name;
            consts->index++;
            const_resize();
            break;
        }
    case DATA_BOOL:
        {
            int bool_len = snprintf(NULL, 0, "_bool%d", const_i);
            const_name = malloc(bool_len + 1);
            if (const_name) {
                snprintf(const_name, bool_len + 1, "_bool%d", const_i++);
            }
            consts->type[consts->index] = type;
            consts->value[consts->index] = strdup(value);
            consts->var_type[consts->index] = TYPE_VAR;
            consts->value_type[consts->index] = value_type;
            consts->name[consts->index++] = strdup(const_name);
            break;
        }

    case DATA_CHAR:
        {
            int char_len = snprintf(NULL, 0, "_char_%d", const_i);
            const_name = malloc(char_len + 1);
            if (const_name) {
                snprintf(const_name, char_len + 1, "_char_%d", const_i++);
            }
            consts->type[consts->index] = type;
            consts->value[consts->index] = strdup(value);
            consts->var_type[consts->index] = TYPE_VAR;
            consts->value_type[consts->index] = value_type;
            consts->name[consts->index++] = strdup(const_name);
            break;
        }

    case DATA_INT:
        {
            int int_len = snprintf(NULL, 0, "_int%d", const_i);
            const_name = malloc(int_len + 1);
            if (const_name) {
                snprintf(const_name, int_len + 1, "_int%d", const_i++);
            }
            consts->type[consts->index] = type;
            consts->value[consts->index] = normalize_number_str(value);
            consts->var_type[consts->index] = TYPE_VAR;
            consts->value_type[consts->index] = value_type;
            consts->name[consts->index++] = strdup(const_name);
            break;
        }

    case DATA_INT64:
    case DATA_UINT64:
        {
            long long val = parse_integer64(value);
            int len = snprintf(NULL, 0, "_int64_%d", const_i);
            const_name = malloc(len + 1);
            if (const_name) {
                snprintf(const_name, len + 1, "_int64_%d", const_i++);
            }
            consts->type[consts->index] = type;
            char hex_str[32];
            snprintf(hex_str, sizeof(hex_str), "0x%llX", (unsigned long long)val);
            consts->value[consts->index] = strdup(hex_str);
            consts->var_type[consts->index] = TYPE_VAR;
            consts->value_type[consts->index] = value_type;
            consts->name[consts->index++] = strdup(const_name);
            break;
        }

    default:
        INTERNAL_ERROR("add_const: unsupported data type %d", type);
    }

    if (const_name == NULL) {
        OUT_OF_MEMORY();
    }

    if (consts->value[consts->index-1] == NULL) {
        OUT_OF_MEMORY();
    }

    return const_name;
}

bool value_is_var(char* value)
{
    for (int i = 0; i < consts->index; i++)
    {
        if (strcmp(consts->name[i], value) == 0)
        {
            if (is_var_name(consts->value[i]) && consts->value_type[i] != DATA_STRING && consts->value_type[i] != DATA_CHAR)
            {
                return true;
            }
        }
    }
    return false;
}


int get_const(char* name)
{
    if (name == NULL)
    {
        return -1;
    }
    for (int i = 0; i < consts->index; i++)
    {
        if (consts->name[i] && strcmp(consts->name[i], name) == 0)
        {
            return i;
        }
    }
    const char* base = strip_ssa_suffix(name);
    if (strchr(name, '@')) {
        for (int i = 0; i < consts->index; i++)
        {
            if (consts->name[i] && strcmp(consts->name[i], base) == 0)
            {
                return i;
            }
        }
    }
    return -1;
}

static const char* canonical_name(const char* name) {
    if (!name) return name;
    if (is_temp(name)) return name;
    if (is_number(name)) return name;
    int idx = get_const((char*)name);
    if (idx >= 0 && consts->name[idx]) {
        return consts->name[idx];
    }
    return name;
}

void print_var_address(FILE* out, char* var_name) {
    if (is_temp(var_name)) {
        int off = temp_map_get_offset(var_name);
        fprintf(out, "[ebp-%d]", off);
        return;
    }
    const char* canon = canonical_name(var_name);
    int idx = get_const((char*)canon);
    if (idx == -1) {
        fprintf(out, "[%s]", var_name);
        return;
    }
    switch (consts->storage[idx]) {
    case STORAGE_GLOBAL:
        fprintf(out, "[%s]", canon);
        break;
    case STORAGE_PARAM:
        fprintf(out, "[ebp+%d]", consts->offset[idx]);
        break;
    case STORAGE_LOCAL:
        fprintf(out, "[ebp-%d]", -consts->offset[idx]);
        break;
    }
}

void load_var_to_eax_or_st0(FILE* out, char* var_name, DataType* loaded_type) {
    if (is_temp(var_name)) {
        int off = temp_map_get_offset(var_name);
        fprintf(out, "  mov eax, dword [ebp-%d]\n", off);
        *loaded_type = DATA_INT;
        return;
    }
    if (is_number(var_name)) {
        if (strchr(var_name, '.') != NULL) {
            char* label = add_const(DATA_FLOAT, var_name, 0, DATA_FLOAT);
            fprintf(out, "  fld dword [%s]\n", label);
            *loaded_type = DATA_FLOAT;
            return;
        }
        fprintf(out, "  mov eax, %s\n", var_name);
        *loaded_type = DATA_INT;
        return;
    }
    const char* canon = canonical_name(var_name);
    int idx = get_const((char*)canon);

    int is_ret_ptr2 = (var_name[0] == '.' && var_name[1] == 'r'
                       && isdigit((unsigned char)var_name[2]));

    if (!is_ret_ptr2 && idx != -1 && consts->is_dynamic_array[idx]) {
        char addr[100];
        switch (consts->storage[idx]) {
            case STORAGE_GLOBAL: sprintf(addr, "[%s]", canon); break;
            case STORAGE_PARAM:  sprintf(addr, "[ebp+%d]", consts->offset[idx]); break;
            case STORAGE_LOCAL:  sprintf(addr, "[ebp-%d]", -consts->offset[idx]); break;
        }
        fprintf(out, "  mov eax, dword %s\n", addr);
        *loaded_type = DATA_INT;
        return;
    }

    if (!is_ret_ptr2 && idx != -1 && consts->is_array[idx]
        && consts->type[idx] != DATA_STR) {
        char addr[100];
        switch (consts->storage[idx]) {
            case STORAGE_GLOBAL: sprintf(addr, "[%s]", canon); break;
            case STORAGE_PARAM:  sprintf(addr, "[ebp+%d]", consts->offset[idx]); break;
            case STORAGE_LOCAL:  sprintf(addr, "[ebp-%d]", -consts->offset[idx]); break;
        }
        fprintf(out, "  lea eax, %s\n", addr);
        *loaded_type = DATA_INT;
        return;
    }

    DataType actual_type = consts->type[idx];
    *loaded_type = actual_type;

    char addr[100];
    switch (consts->storage[idx]) {
    case STORAGE_GLOBAL: sprintf(addr, "[%s]", canon); break;
    case STORAGE_PARAM:  sprintf(addr, "[ebp+%d]", consts->offset[idx]); break;
    case STORAGE_LOCAL:  sprintf(addr, "[ebp-%d]", -consts->offset[idx]); break;
    }

    if (actual_type == DATA_FLOAT || actual_type == DATA_UFLOAT) {
        fprintf(out, "  fld dword %s\n", addr);
    } else if (actual_type == DATA_CHAR || actual_type == DATA_BOOL) {
        fprintf(out, "  movzx eax, byte %s\n", addr);
    } else {
        fprintf(out, "  mov eax, dword %s\n", addr);
    }
}

DataType get_actual_type(char* name, DataType declared_type) {
    if (declared_type == DATA_IDENTIFIER) {
        int idx = get_const(name);
        if (idx != -1) {
            return consts->type[idx];
        }
    }
    return declared_type;
}

DataType get_type(char* operand, DataType declared) {
    if (is_temp(operand)) {
        DataType t = temp_map_get(operand);
        if (t != DATA_ERROR) return t;
    } else {
        int idx = get_const(operand);
        if (idx != -1) return consts->type[idx];
    }
    return declared;
}

void add_symbol(DataType type, char* value, char* name,
                Var_Type var_type, DataType value_type,
                StorageClass storage, int offset,
                 bool is_array, int array_size,bool is_dynamic_array)
{
    const_resize();
    int idx = consts->index;
    consts->type[idx] = type;
    consts->value[idx] = strdup(value);
    consts->var_type[idx] = var_type;
    consts->name[idx] = strdup(strip_ssa_suffix(name));
    consts->value_type[idx] = value_type;
    consts->storage[idx] = storage;
    consts->offset[idx] = offset;
    consts->is_array[idx] = is_array;
    consts->array_size[idx] = array_size;
    consts->is_dynamic_array[idx] = is_dynamic_array;
    consts->index++;
}

int get_type_code(DataType type) {
    switch (type) {
    case DATA_INT: return 0;
    case DATA_FLOAT: return 1;
    case DATA_CHAR: return 2;
    case DATA_STRING: return 3;
    case DATA_BOOL: return 4;
    default: return 0;
    }
}

static int map_to_runtime_type(DataType dt) {
    switch (dt) {
    case DATA_INT:    case DATA_INT8:  case DATA_INT16:
    case DATA_INT32:  case DATA_INT64: case DATA_INT128:
        return 0;
    case DATA_CHAR:
        return 2;
    case DATA_FLOAT:  case DATA_FLOAT8:  case DATA_FLOAT16:
    case DATA_FLOAT32: case DATA_FLOAT64: case DATA_FLOAT128:
        return 1;
    case DATA_STRING: return 3;
    case DATA_BOOL:   return 4;
    case DATA_UINT:   case DATA_UINT8:  case DATA_UINT16:
    case DATA_UINT32: case DATA_UINT64: case DATA_UINT128:
        return 5;
    case DATA_UFLOAT: case DATA_UFLOAT8:  case DATA_UFLOAT16:
    case DATA_UFLOAT32: case DATA_UFLOAT64: case DATA_UFLOAT128:
        return 6;
    case DATA_STR:    return 3;
    default:          return 0;
    }
}

static DataType cg_load(FILE* out, const char* name, DataType hint_type) {
    if (name == NULL) return DATA_INT;
    DataType original_hint = hint_type;
    hint_type = normalize_type(hint_type);

    if (hint_type == DATA_STR) {
        hint_type = DATA_INT;
    }

    if (is_number((char*)name)) {
        if (strchr(name, '.') != NULL) {
            char* label = add_const(DATA_FLOAT, (char*)name, 0, DATA_FLOAT);
            fprintf(out, "  fld dword [%s]\n", label);
            return DATA_FLOAT;
        }
        long long val64 = parse_integer64(name);
        unsigned long long uval64 = (unsigned long long)val64;
        if (uval64 > 0x7FFFFFFFULL || val64 < -2147483648LL) {
            char* const_label = add_const(DATA_INT64, (char*)name, 0, DATA_INT64);
            cache_flush_reg(out, R_EDX);
            fprintf(out, "  mov eax, dword [%s]\n", const_label);
            fprintf(out, "  mov edx, dword [%s+4]\n", const_label);
            return DATA_INT64;
        } else {
            fprintf(out, "  mov eax, %s\n", name);
        }
        return DATA_INT;
    }

    int slot = -1;
    if (!strrchr(name, '@'))
        slot = cache_find(name);
    if (slot >= 0) {
        fprintf(out, "  mov eax, %s\n", CG_REG_NAMES[cache_slots[slot].reg]);
        int ti = get_const((char*)name);
        if (ti != -1) return consts->type[ti];
        return DATA_INT;
    }

    int idx = get_const((char*)name);
    if (idx != -1) {
        DataType type = consts->type[idx];
        char addr[100];

        switch (consts->storage[idx]) {
        case STORAGE_GLOBAL:
            snprintf(addr, sizeof(addr), "[%s]",
                     consts->name[idx] ? consts->name[idx] : name);
            break;
        case STORAGE_PARAM:
            snprintf(addr, sizeof(addr), "[ebp+%d]", consts->offset[idx]);
            break;
        case STORAGE_LOCAL:
            snprintf(addr, sizeof(addr), "[ebp-%d]", -consts->offset[idx]);
            break;
        case STORAGE_STATIC_LOCAL:
        case STORAGE_STATIC_GUARD:
            snprintf(addr, sizeof(addr), "[%s]",
                     consts->name[idx] ? consts->name[idx] : name);
            break;
        default:
            snprintf(addr, sizeof(addr), "[%s]",
                     consts->name[idx] ? consts->name[idx] : name);
            break;
        }

        int is_ret_ptr = (name[0] == '.' && name[1] == 'r'
                          && isdigit((unsigned char)name[2]));
        if (!is_ret_ptr && consts->is_dynamic_array[idx]) {
            fprintf(out, "  mov eax, dword %s\n", addr);
            return DATA_INT;
        }
        if (!is_ret_ptr && consts->is_array[idx]
            && type != DATA_STR
            && type != DATA_INT64 && type != DATA_UINT64
            && type != DATA_FLOAT64 && type != DATA_UFLOAT64) {
            fprintf(out, "  lea eax, %s\n", addr);
            return DATA_INT;
        }
        if (normalize_type(type) == DATA_FLOAT || normalize_type(type) == DATA_UFLOAT) {
            int sz = get_type_size(type);
            if (sz == 1) {
                fprintf(out, "  movzx eax, byte %s\n", addr);
                fprintf(out, "  push eax\n");
                fprintf(out, "  fild dword [esp]\n");
                fprintf(out, "  add esp, 4\n");
            } else if (sz == 2) {
                fprintf(out, "  fild word %s\n", addr);
            } else if (sz >= 8)
                fprintf(out, "  fld qword %s\n", addr);
            else
                fprintf(out, "  fld dword %s\n", addr);
        } else if (type == DATA_CHAR || type == DATA_BOOL ||
                   type == DATA_INT8 || type == DATA_UINT8) {
            fprintf(out, "  movzx eax, byte %s\n", addr);
        } else if (type == DATA_INT16) {
            fprintf(out, "  movsx eax, word %s\n", addr);
        } else if (type == DATA_UINT16) {
            fprintf(out, "  movzx eax, word %s\n", addr);
        } else if (type == DATA_STR) {
            size_t alen = strlen(addr);
            cache_flush_reg(out, R_EDX);
            if (alen > 0 && addr[alen-1] == ']') {
                addr[alen-1] = '\0';
                fprintf(out, "  mov eax, dword %s]\n", addr);
                fprintf(out, "  mov edx, dword %s+4]\n", addr);
            } else {
                fprintf(out, "  mov eax, dword %s\n", addr);
                fprintf(out, "  mov edx, dword [%s+4]\n", addr);
            }
        } else if (original_hint == DATA_INT64 || original_hint == DATA_UINT64) {
            cache_flush_reg(out, R_EDX);
            if (strncmp(addr, "[ebp-", 5) == 0) {
                int base_n = atoi(addr + 5);
                fprintf(out, "  mov eax, dword [ebp-%d]\n", base_n);
                fprintf(out, "  mov edx, dword [ebp-%d]\n", base_n - 4);
            } else if (strncmp(addr, "[ebp+", 5) == 0) {
                int base_n = atoi(addr + 5);
                fprintf(out, "  mov eax, dword [ebp+%d]\n", base_n);
                fprintf(out, "  mov edx, dword [ebp+%d]\n", base_n + 4);
            } else {
                size_t alen = strlen(addr);
                if (alen > 0 && addr[alen-1] == ']') {
                    addr[alen-1] = '\0';
                    fprintf(out, "  mov eax, dword %s]\n", addr);
                    fprintf(out, "  mov edx, dword %s+4]\n", addr);
                } else {
                    fprintf(out, "  mov eax, dword %s\n", addr);
                    fprintf(out, "  mov edx, dword [%s+4]\n", addr);
                }
            }
        } else {
            fprintf(out, "  mov eax, dword %s\n", addr);
        }
        char raddr[96];
        const RegMapEntry* e = strrchr(name, '@') ? NULL
                                                  : regmap_find_resident(name);
        if (e && (e->reg == R_ECX || e->reg == R_EDX ||
                  e->reg == R_EBX || e->reg == R_ESI || e->reg == R_EDI)
            && consts->storage[idx] == STORAGE_LOCAL
            && !consts->is_array[idx]
            && !consts->is_dynamic_array[idx]
            && get_type_size(type) == 4
            && !(normalize_type(type) == DATA_FLOAT
                 || normalize_type(type) == DATA_UFLOAT)
            && !var_escapes(name)
            && resident_addr(name, raddr, sizeof(raddr))) {
            int si = e->reg - R_ECX;
            cache_flush_slot(out, si);
            fprintf(out, "  mov %s, eax\n", CG_REG_NAMES[e->reg]);
            cache_install(si, name, e->reg, raddr);
        }
        return type;
    }

    if (is_temp(name)) {
        int off = temp_map_get_offset(name);
        DataType t = temp_map_get((char*)name);
        if (t == DATA_ERROR) t = hint_type;
        if (t == DATA_FLOAT || t == DATA_UFLOAT || t == DATA_FLOAT64 || t == DATA_UFLOAT64 ||
            t == DATA_FLOAT8 || t == DATA_FLOAT16 || t == DATA_FLOAT32 || t == DATA_FLOAT128 ||
            t == DATA_UFLOAT8 || t == DATA_UFLOAT16 || t == DATA_UFLOAT32 || t == DATA_UFLOAT128) {
            int sz = get_type_size(t);
            if (sz == 1) {
                fprintf(out, "  movzx eax, byte [ebp-%d]\n", off);
                fprintf(out, "  push eax\n");
                fprintf(out, "  fild dword [esp]\n");
                fprintf(out, "  add esp, 4\n");
            } else if (sz == 2) {
                fprintf(out, "  fild word [ebp-%d]\n", off);
            } else if (sz >= 8)
                fprintf(out, "  fld qword [ebp-%d]\n", off);
            else
                fprintf(out, "  fld dword [ebp-%d]\n", off);
            return t;
        }
        if (t == DATA_CHAR || t == DATA_BOOL) {
            fprintf(out, "  movzx eax, byte [ebp-%d]\n", off);
        } else if (t == DATA_INT64 || t == DATA_UINT64) {
            cache_flush_reg(out, R_EDX);
            fprintf(out, "  mov eax, dword [ebp-%d]\n", off);
            fprintf(out, "  mov edx, dword [ebp-%d]\n", off - 4);
        } else if (t == DATA_FLOAT64 || t == DATA_UFLOAT64) {
            fprintf(out, "  fld qword [ebp-%d]\n", off);
        } else if (t == DATA_STR) {
            cache_flush_reg(out, R_EDX);
            fprintf(out, "  mov eax, dword [ebp-%d]\n", off);
            fprintf(out, "  mov edx, dword [ebp-%d]\n", off - 4);
        } else {
            fprintf(out, "  mov eax, dword [ebp-%d]\n", off);
        }
        return t;
    }

    const RegMapEntry* e = regmap_get(g_regmap, name);
    if (e != NULL && e->reg != R_NONE && !e->is_float) {
        if (e->reg == R_EAX) {
        } else {
            fprintf(out, "  mov eax, %s\n",
                    (const char*[]){"eax","ecx","edx","ebx","esi","edi"}[e->reg]);
        }
        return hint_type;
    }
    if (e != NULL && e->is_float && e->reg != R_NONE) {
        fprintf(out, "  fld dword [ebp-%d]\n", e->spill_slot);
        return DATA_FLOAT;
    }
    if (e != NULL && e->reg == R_NONE) {
        fprintf(out, "  mov eax, dword [ebp-%d]\n", spill_base + e->spill_slot);
        return hint_type;
    }

    if (strncmp(name, "_var", 4) == 0
        && get_const((char*)name) == -1) {
        add_symbol(DATA_INT, "0", (char*)name, TYPE_VAR, DATA_INT,
                   STORAGE_GLOBAL, 0, false, 0, false);
    }
    fprintf(out, "  mov eax, %s\n", name);
    return DATA_INT;
}

static void cg_store(FILE* out, const char* name, DataType dt, bool is_float) {
    if (name == NULL) return;
    DataType original_dt = dt;
    dt = normalize_type(dt);
    if (!is_float && current_regmap && !strrchr(name, '@')
        && get_type_size(original_dt) == 4) {
        int idx = get_const((char*)name);
        if (idx != -1
            && consts->storage[idx] == STORAGE_LOCAL
            && !consts->is_array[idx]
            && !consts->is_dynamic_array[idx]
            && !var_escapes(name)) {
            const RegMapEntry* e = regmap_find_resident(name);
            if (e && (e->reg == R_ECX || e->reg == R_EDX ||
                      e->reg == R_EBX || e->reg == R_ESI || e->reg == R_EDI)) {
                int slot = cache_find(name);
                if (slot >= 0) {
                    fprintf(out, "  mov %s, eax\n",
                            CG_REG_NAMES[cache_slots[slot].reg]);
                    if (cache_slots[slot].has_addr) {
                        fprintf(out, "  mov dword %s, eax\n",
                                cache_slots[slot].addr);
                    }
                    return;
                }
                char raddr[96];
                if (resident_addr(name, raddr, sizeof(raddr))) {
                    int si = e->reg - R_ECX;
                    cache_flush_slot(out, si);
                    fprintf(out, "  mov %s, eax\n", CG_REG_NAMES[e->reg]);
                    fprintf(out, "  mov dword %s, eax\n", raddr);
                    cache_install(si, name, e->reg, raddr);
                    return;
                }
                      }
            }
        }
    cache_invalidate(out, name);

    int idx = get_const((char*)name);
    if (idx != -1) {
        char addr[100];
        switch (consts->storage[idx]) {
        case STORAGE_GLOBAL:
            snprintf(addr, sizeof(addr), "[%s]",
                     consts->name[idx] ? consts->name[idx] : name);
            break;
        case STORAGE_PARAM:
            snprintf(addr, sizeof(addr), "[ebp+%d]", consts->offset[idx]);
            break;
        case STORAGE_LOCAL:
            snprintf(addr, sizeof(addr), "[ebp-%d]", -consts->offset[idx]);
            break;
        case STORAGE_STATIC_LOCAL:
        case STORAGE_STATIC_GUARD:
            snprintf(addr, sizeof(addr), "[%s]",
                     consts->name[idx] ? consts->name[idx] : name);
            break;
        default:
            return;
        }

        if (is_float || normalize_type(original_dt) == DATA_FLOAT || normalize_type(original_dt) == DATA_UFLOAT) {
            int sz = get_type_size(original_dt);
            if (sz == 1) {
                fprintf(out, "  sub esp, 4\n");
                fprintf(out, "  fisttp dword [esp]\n");
                fprintf(out, "  mov al, byte [esp]\n");
                fprintf(out, "  add esp, 4\n");
                fprintf(out, "  mov byte %s, al\n", addr);
            } else if (sz == 2) {
                fprintf(out, "  sub esp, 4\n");
                fprintf(out, "  fisttp dword [esp]\n");
                fprintf(out, "  mov ax, word [esp]\n");
                fprintf(out, "  add esp, 4\n");
                fprintf(out, "  mov word %s, ax\n", addr);
            } else if (sz == 4)
                fprintf(out, "  fstp dword %s\n", addr);
            else if (sz == 8)
                fprintf(out, "  fstp qword %s\n", addr);
            else
                fprintf(out, "  ; WARNING: unsupported float size %d, using dword\n", sz);
        } else if (dt == DATA_CHAR || dt == DATA_BOOL ||
                   original_dt == DATA_INT8 || original_dt == DATA_UINT8) {
            fprintf(out, "  mov byte %s, al\n", addr);
        } else if (original_dt == DATA_INT16 || original_dt == DATA_UINT16) {
            fprintf(out, "  mov word %s, ax\n", addr);
        } else if (original_dt  == DATA_INT64 || original_dt  == DATA_UINT64
                   || original_dt == DATA_STR) {
            if (strncmp(addr, "[ebp-", 5) == 0) {
                int base_n = atoi(addr + 5);
                fprintf(out, "  mov dword [ebp-%d], eax\n", base_n);
                fprintf(out, "  mov dword [ebp-%d], edx\n", base_n - 4);
            } else if (strncmp(addr, "[ebp+", 5) == 0) {
                int base_n = atoi(addr + 5);
                fprintf(out, "  mov dword [ebp+%d], eax\n", base_n);
                fprintf(out, "  mov dword [ebp+%d], edx\n", base_n + 4);
            } else {
                size_t alen = strlen(addr);
                if (alen > 0 && addr[alen-1] == ']') {
                    addr[alen-1] = '\0';
                    fprintf(out, "  mov dword %s], eax\n", addr);
                    fprintf(out, "  mov dword %s+4], edx\n", addr);
                } else {
                    fprintf(out, "  mov dword %s, eax\n", addr);
                    fprintf(out, "  mov dword [%s+4], edx\n", addr);
                }
            }
        } else if (dt == DATA_INT128 || dt == DATA_UINT128) {
            fprintf(out, "  ; WARNING: 128-bit store as 32-bit\n");
            fprintf(out, "  mov dword %s, eax\n", addr);
        } else {
            fprintf(out, "  mov dword %s, eax\n", addr);
        }
        return;
    }


    if (is_temp(name)) {
        int off = temp_map_get_offset(name);
        DataType temp_dt = temp_map_get((char*)name);
        if (temp_dt == DATA_ERROR) temp_dt = original_dt;
        if (is_float || original_dt == DATA_FLOAT || original_dt == DATA_UFLOAT ||
            original_dt == DATA_FLOAT64 || original_dt == DATA_UFLOAT64 ||
            original_dt == DATA_FLOAT8 || original_dt == DATA_FLOAT16 ||
            original_dt == DATA_FLOAT32 || original_dt == DATA_FLOAT128 ||
            original_dt == DATA_UFLOAT8 || original_dt == DATA_UFLOAT16 ||
            original_dt == DATA_UFLOAT32 || original_dt == DATA_UFLOAT128) {
            int sz = get_type_size(original_dt);
            if (sz == 1) {
                fprintf(out, "  sub esp, 4\n");
                fprintf(out, "  fisttp dword [esp]\n");
                fprintf(out, "  mov al, byte [esp]\n");
                fprintf(out, "  add esp, 4\n");
                fprintf(out, "  mov byte [ebp-%d], al\n", off);
            } else if (sz == 2) {
                fprintf(out, "  sub esp, 4\n");
                fprintf(out, "  fisttp dword [esp]\n");
                fprintf(out, "  mov ax, word [esp]\n");
                fprintf(out, "  add esp, 4\n");
                fprintf(out, "  mov word [ebp-%d], ax\n", off);
            } else if (sz >= 8)
                fprintf(out, "  fstp qword [ebp-%d]\n", off);
            else
                fprintf(out, "  fstp dword [ebp-%d]\n", off);
        } else if (temp_dt == DATA_INT64 || temp_dt == DATA_UINT64 ||
                   original_dt == DATA_INT64 || original_dt == DATA_UINT64 ||
                   temp_dt == DATA_STR || original_dt == DATA_STR) {
            fprintf(out, "  mov dword [ebp-%d], eax\n", off);
            fprintf(out, "  mov dword [ebp-%d], edx\n", off - 4);
        } else if (original_dt == DATA_CHAR || original_dt == DATA_BOOL)
            fprintf(out, "  mov dword [ebp-%d], eax\n", off);
        else
            fprintf(out, "  mov dword [ebp-%d], eax\n", off);
        return;
    }

    const RegMapEntry* e = regmap_get(g_regmap, name);
    if (e != NULL && e->reg != R_NONE && !e->is_float) {
        if (e->reg != R_EAX) {
            fprintf(out, "  mov %s, eax\n",
                    (const char*[]){"eax","ecx","edx","ebx","esi","edi"}[e->reg]);
        }
        return;
    }
    if (e != NULL && e->spill_slot > 0) {
        if (is_float || dt == DATA_FLOAT || dt == DATA_UFLOAT)
            fprintf(out, "  fstp dword [ebp-%d]\n", spill_base + e->spill_slot);
        else
            fprintf(out, "  mov dword [ebp-%d], eax\n", spill_base + e->spill_slot);
        return;
    }

    FATAL(ERR_CG_UNKNOWN_VAR, LOC_NONE,
          "cg_store: cannot store to unknown variable '%s'", name);
}

static bool cg_address(FILE* out, const char* name, char* addr_buf, int buf_len) {
    if (name == NULL) { snprintf(addr_buf, buf_len, "0"); return true; }
    if (cache_find(name) >= 0) {
        snprintf(addr_buf, buf_len, "0");
        return false;
    }
    const char* canon = canonical_name(name);
    if (is_temp(canon)) {
        int off = temp_map_get_offset(canon);
        snprintf(addr_buf, buf_len, "[ebp-%d]", off);
        return true;
    }
    const RegMapEntry* e = regmap_get(g_regmap, canon);
    if (e != NULL && e->reg != R_NONE) {
        if (e->spill_slot > 0) {
            snprintf(addr_buf, buf_len, "[ebp-%d]", spill_base + e->spill_slot);
            return true;
        }
        snprintf(addr_buf, buf_len, "0");
        return false;
    }
    if (e != NULL && e->reg == R_NONE) {
        snprintf(addr_buf, buf_len, "[ebp-%d]", spill_base + e->spill_slot);
        return true;
    }
    int idx = get_const((char*)canon);
    if (idx == -1) {
        snprintf(addr_buf, buf_len, "[%s]", canon);
        return true;
    }
    switch (consts->storage[idx]) {
    case STORAGE_GLOBAL: snprintf(addr_buf, buf_len, "[%s]", canon); break;
    case STORAGE_PARAM:  snprintf(addr_buf, buf_len, "[ebp+%d]", consts->offset[idx]); break;
    case STORAGE_LOCAL:  snprintf(addr_buf, buf_len, "[ebp-%d]", -consts->offset[idx]); break;
    default:             snprintf(addr_buf, buf_len, "[%s]", canon); break;
    }
    return true;
}

static void cg_load_float(FILE* out, const char* name, DataType hint_type) {
    if (name == NULL) return;
    DataType orig_hint = hint_type;
    hint_type = normalize_type(hint_type);

    bool is_int_src = (hint_type == DATA_INT  || hint_type == DATA_UINT ||
                       hint_type == DATA_CHAR || hint_type == DATA_BOOL);

    if (is_temp(name)) {
        int off = temp_map_get_offset(name);
        if (is_int_src) {
            if (hint_type == DATA_UINT) {
                fprintf(out, "  push 0\n");
                fprintf(out, "  push dword [ebp-%d]\n", off);
                fprintf(out, "  fild qword [esp]\n");
                fprintf(out, "  add esp, 8\n");
            } else {
                fprintf(out, "  fild dword [ebp-%d]\n", off);
            }
        } else {
            int sz = get_type_size(orig_hint);
            if (sz == 1) {
                fprintf(out, "  movzx eax, byte [ebp-%d]\n", off);
                fprintf(out, "  push eax\n");
                fprintf(out, "  fild dword [esp]\n");
                fprintf(out, "  add esp, 4\n");
            } else if (sz == 2) {
                fprintf(out, "  fild word [ebp-%d]\n", off);
            } else if (sz <= 4)
                fprintf(out, "  fld dword [ebp-%d]\n", off);
            else if (sz == 8)
                fprintf(out, "  fld qword [ebp-%d]\n", off);
            else
                fprintf(out, "  fld dword [ebp-%d]\n", off);
        }
        return;
    }

    int idx = get_const(name);
    if (idx != -1) {
        char addr[100];
        switch (consts->storage[idx]) {
        case STORAGE_GLOBAL: snprintf(addr, sizeof(addr), "[%s]", name); break;
        case STORAGE_PARAM:  snprintf(addr, sizeof(addr), "[ebp+%d]", consts->offset[idx]); break;
        case STORAGE_LOCAL:  snprintf(addr, sizeof(addr), "[ebp-%d]", -consts->offset[idx]); break;
        default:             snprintf(addr, sizeof(addr), "[%s]", name); break;
        }
        if (is_int_src) {
            if (hint_type == DATA_UINT) {
                fprintf(out, "  push 0\n");
                fprintf(out, "  push dword %s\n", addr);
                fprintf(out, "  fild qword [esp]\n");
                fprintf(out, "  add esp, 8\n");
            } else {
                fprintf(out, "  fild dword %s\n", addr);
            }
        } else {
            int sz = get_type_size(orig_hint);
            if (sz == 1) {
                fprintf(out, "  movzx eax, byte %s\n", addr);
                fprintf(out, "  push eax\n");
                fprintf(out, "  fild dword [esp]\n");
                fprintf(out, "  add esp, 4\n");
            } else if (sz == 2) {
                fprintf(out, "  fild word %s\n", addr);
            } else if (sz <= 4)
                fprintf(out, "  fld dword %s\n", addr);
            else if (sz == 8)
                fprintf(out, "  fld qword %s\n", addr);
            else
                fprintf(out, "  fld dword %s\n", addr);
        }
        return;
    }

    char* label = add_const(DATA_FLOAT, (char*)name, 0, DATA_FLOAT);
    fprintf(out, "  fld dword [%s]\n", label);
}

void codegen_win32_init()
{
    for (int i = 0; i < 5; i++) {
        cache_slots[i].reg = (Reg)(R_ECX + i);
    }
    consts = calloc(1,sizeof(Const));
    if (consts == NULL) { OUT_OF_MEMORY(); }
    consts->index = 0;
    consts->size = 16;
    consts->type = calloc(consts->size,sizeof(DataType));
    if (consts->type == NULL) { OUT_OF_MEMORY(); }
    consts->value = calloc(consts->size,sizeof(char*));
    if (consts->value == NULL) { OUT_OF_MEMORY(); }
    consts->name = calloc(consts->size,sizeof(char*));
    if (consts->name == NULL) { OUT_OF_MEMORY(); }
    consts->var_type = calloc(consts->size,sizeof(Var_Type));
    if (consts->var_type == NULL) { OUT_OF_MEMORY(); }
    consts->value_type = calloc(consts->size,sizeof(DataType));
    if (consts->value_type == NULL) { OUT_OF_MEMORY(); }
    consts->storage = calloc(consts->size,sizeof(StorageClass));
    if (consts->storage == NULL) { OUT_OF_MEMORY(); }
    consts->offset = calloc(consts->size,sizeof(int));
    if (consts->offset == NULL) { OUT_OF_MEMORY(); }
    consts->is_array = calloc(consts->size,sizeof(bool));
    if (consts->is_array == NULL) { OUT_OF_MEMORY(); }
    consts->array_size = calloc(consts->size,sizeof(int));
    if (consts->array_size == NULL) { OUT_OF_MEMORY(); }
    consts->is_dynamic_array = calloc(consts->size, sizeof(bool));
    if (consts->is_dynamic_array == NULL) { OUT_OF_MEMORY(); }
    temp_map_init();
}

void codegen_win32_start(FILE* output)
{
}

void codegen_win32(FILE* output,IR* ir)
{
    if (ir == NULL)
    {
        INTERNAL_ERROR("codegen: NULL IR");
    }

    in_function = false;
    current_local_offset = 0;
    current_func_end = NULL;
    current_param_offset = 8;
    current_param_index = 0;
    in_main = false;

    {
        ClassEntry* cls = semantics_get_all_classes();
        while (cls) {
            for (int i = 0; i < cls->member_count; i++) {
                MemberInfo* mi = cls->members[i];
                if (!mi->is_static || mi->is_method) continue;
                if (mi->dim_count > 0) continue;
                const char* sym_name = mi->field_class_name
                                       ? mi->field_class_name
                                       : mi->name;
                if (!sym_name) continue;
                if (get_const((char*)sym_name) != -1) continue;
                const char* init_str = "0";
                if (mi->const_value_str) {
                    init_str = mi->const_value_str;
                } else if (mi->init_value_str) {
                    init_str = mi->init_value_str;
                }
                add_symbol(mi->type, (char*)init_str, (char*)sym_name,
                           TYPE_VAR, mi->type, STORAGE_GLOBAL, 0,
                           false, 0, false);
            }
            cls = cls->next;
        }
    }

    for (IR* ir_pre = ir; ir_pre; ir_pre = ir_pre->next) {
        if (ir_pre->type == IR_ALLOC_CLASS) {
            has_gc = true;
            has_memory_lib = true;
        }
        if (ir_pre->type == IR_CALL && ir_pre->args1) {
            if (strcmp(ir_pre->args1, "_gc_alloc") == 0 ||
                strcmp(ir_pre->args1, "_gc_collect") == 0 ||
                strcmp(ir_pre->args1, "_gc_force") == 0 ||
                strcmp(ir_pre->args1, "_gc_status") == 0 ||
                strcmp(ir_pre->args1, "_gc_alloc_ptr_array") == 0 ||
                strcmp(ir_pre->args1, "_AllocMemory") == 0) {
                has_gc = true;
                has_memory_lib = true;
            }
            if (strncmp(ir_pre->args1, "_sqlite_", 8) == 0) {
                has_sqlite = true;
            }
        }
    }

    cache_flush_all(output);
    current_regmap = NULL;

    IR* ir_temp = ir;
    static unsigned current_func_callee_mask = 0;

    ir_temp = ir;
    while (ir_temp != NULL)
    {
        if (!in_function) {
            switch (ir_temp->type) {
            case IR_VAR:
            case IR_VAR_UNINIT:
            case IR_ARRAY_DECL:
            case IR_LABEL:
            case IR_PARAM_DEF:
                break;
            default:
                ir_temp = ir_temp->next;
                continue;
            }
        }
        switch (ir_temp->type) {
            case IR_LITERAL: {
                if (ir_temp->result && ir_temp->args1 &&
                    strcmp(ir_temp->result, ir_temp->args1) == 0
                    && !ir_temp->returns_array) {
                    break;
                }

                if (ir_temp->returns_array && ir_temp->hidden_ret_buf) {
                    int cb_off = temp_map_get_offset(ir_temp->hidden_ret_buf);
                    int res_off = temp_map_get_offset(ir_temp->result);
                    fprintf(output, "  mov ecx, dword [ebp-%d]\n", cb_off);
                    fprintf(output, "  mov eax, dword [ecx]\n");
                    fprintf(output, "  mov edx, dword [ecx+4]\n");
                    fprintf(output, "  mov dword [ebp-%d], eax\n", res_off);
                    fprintf(output, "  mov dword [ebp-%d], edx\n", res_off - 4);
                    temp_map_add(ir_temp->result, ir_temp->data_type);
                    break;
                }

                DataType dst_type = ir_temp->data_type;
                char* src = ir_temp->args1;
                DataType src_type = ir_temp->args1_type;

                if (dst_type == DATA_STRING &&
                    !is_temp(src) && get_const(src) == -1) {
                    char* const_label = add_const(DATA_STRING, src, 0, DATA_STRING);
                    fprintf(output, "  mov eax, %s\n", const_label);
                    cg_store(output, ir_temp->result, DATA_STRING, false);
                    } else if ((dst_type == DATA_FLOAT || dst_type == DATA_UFLOAT ||
                             dst_type == DATA_FLOAT8 || dst_type == DATA_FLOAT16 ||
                             dst_type == DATA_FLOAT32 || dst_type == DATA_FLOAT64 ||
                             dst_type == DATA_FLOAT128 ||
                             dst_type == DATA_UFLOAT8 || dst_type == DATA_UFLOAT16 ||
                             dst_type == DATA_UFLOAT32 || dst_type == DATA_UFLOAT64 ||
                             dst_type == DATA_UFLOAT128) &&
                        !is_temp(src) && get_const(src) == -1) {
                        if (is_number(src) || src[0] == '.' ||
                         (src[0] >= '0' && src[0] <= '9') || src[0] == '-') {
                            char* const_label = add_const(DATA_FLOAT, src, 0, DATA_FLOAT);
                            int sz = get_type_size(dst_type);
                            if (sz <= 4)
                                fprintf(output, "  fld dword [%s]\n", const_label);
                            else if (sz == 8)
                                fprintf(output, "  fld qword [%s]\n", const_label);
                            else
                                fprintf(output, "  fld dword [%s]\n", const_label);
                         } else {
                             DataType vlt = cg_load(output, src, src_type);
                             cg_store(output, ir_temp->result, dst_type,
                                      (vlt == DATA_FLOAT || vlt == DATA_UFLOAT
                                       || vlt == DATA_FLOAT32 || vlt == DATA_UFLOAT32));
                             break;
                         }
                        cg_store(output, ir_temp->result, dst_type, true);
                        } else if (dst_type == DATA_CHAR && !is_temp(src)
                                   && get_const(src) == -1
                                   && !is_number((char*)src)) {
                            char ch = src[0];
                            if (src[0] == '\'') ch = src[1];
                            fprintf(output, "  mov eax, %d\n", ch);
                            cg_store(output, ir_temp->result, DATA_CHAR, false);
                        } else if (dst_type == DATA_BOOL && !is_temp(src) && get_const(src) == -1) {
                            int val = (strcmp(src, "true") == 0 || strcmp(src, "1") == 0) ? 1 : 0;
                            fprintf(output, "  mov eax, %d\n", val);
                            cg_store(output, ir_temp->result, DATA_BOOL, false);
                        } else if ((dst_type == DATA_INT64 || dst_type == DATA_UINT64)
                                   && !is_temp(src) && get_const(src) == -1 && is_number(src)) {
                            char* const_label = add_const(dst_type, src, 0, dst_type);
                            cache_flush_reg(output, R_EDX);
                            fprintf(output, "  mov eax, dword [%s]\n", const_label);
                            fprintf(output, "  mov edx, dword [%s+4]\n", const_label);
                            cg_store(output, ir_temp->result, dst_type, false);
                                   } else if (!is_temp(src) && get_const(src) == -1
                                              && is_number(src)
                                              && strchr(src, '.') != NULL) {
                                       char* const_label = add_const(DATA_FLOAT, src, 0, DATA_FLOAT);
                                       fprintf(output, "  fld dword [%s]\n", const_label);
                                       fprintf(output, "  sub esp, 4\n");
                                       fprintf(output, "  fisttp dword [esp]\n");
                                       fprintf(output, "  mov eax, dword [esp]\n");
                                       fprintf(output, "  add esp, 4\n");
                                       cg_store(output, ir_temp->result, dst_type, false);
                                       break;
                                   } else if (!is_temp(src) && get_const(src) == -1 && is_number(src)) {
                                       long long val64 = parse_integer64(src);
                                       int is_overflow = (src[0] != '-' && val64 < 0);
                                       if (val64 > 2147483647LL || val64 < -2147483648LL || is_overflow) {
                                           DataType ct = (src[0] == '-' && !is_overflow) ? DATA_INT64 : DATA_UINT64;
                                           char* const_label = add_const(ct, src, 0, ct);
                                           cache_flush_reg(output, R_EDX);
                                           fprintf(output, "  mov eax, dword [%s]\n", const_label);
                                           fprintf(output, "  mov edx, dword [%s+4]\n", const_label);
                                           cg_store(output, ir_temp->result, ct, false);
                                       } else {
                                           fprintf(output, "  mov eax, %s\n", src);
                                           cg_store(output, ir_temp->result, dst_type, false);
                                       }
                                       break;
                                   } else {
                                       DataType loaded_type = cg_load(output, src, src_type);
                                       bool src_is_array_addr = false;
                                       {
                                           int sidx = get_const(src);
                                           if (sidx != -1 && (consts->is_array[sidx]
                                               || consts->is_dynamic_array[sidx]))
                                               src_is_array_addr = true;
                                       }
                                       if (dst_type == DATA_IDENTIFIER) {
                                           fprintf(output, "  test eax, eax\n");
                                           fprintf(output, "  jz _null_ptr_error\n");
                                           has_bounds_check = true;
                                       }
                                       if (!src_is_array_addr && (dst_type == DATA_FLOAT || dst_type == DATA_UFLOAT)
                                           && !(loaded_type == DATA_FLOAT || loaded_type == DATA_UFLOAT || loaded_type == DATA_FLOAT32 || loaded_type == DATA_UFLOAT32)) {
                                           fprintf(output, "  fld dword [eax]\n");
                                           loaded_type = dst_type;
                                       }
                                       if ((dst_type == DATA_INT || dst_type == DATA_UINT ||
                                           dst_type == DATA_CHAR || dst_type == DATA_BOOL)
                                           && (loaded_type == DATA_FLOAT || loaded_type == DATA_UFLOAT)) {
                                           fprintf(output, "  sub esp, 4\n");
                                           fprintf(output, "  fisttp dword [esp]\n");
                                           fprintf(output, "  mov eax, dword [esp]\n");
                                           fprintf(output, "  add esp, 4\n");
                                           loaded_type = dst_type;
                                       }
                                       cg_store(output, ir_temp->result, dst_type,
                                                (loaded_type == DATA_FLOAT || loaded_type == DATA_UFLOAT ||
                                                 loaded_type == DATA_FLOAT64 || loaded_type == DATA_UFLOAT64 ||
                                                 loaded_type == DATA_FLOAT8 || loaded_type == DATA_FLOAT16 ||
                                                 loaded_type == DATA_FLOAT32 || loaded_type == DATA_FLOAT128 ||
                                                 loaded_type == DATA_UFLOAT8 || loaded_type == DATA_UFLOAT16 ||
                                                 loaded_type == DATA_UFLOAT32 || loaded_type == DATA_UFLOAT128));
                                   }

                if (ir_temp->result && is_temp(ir_temp->result)) {
                    temp_map_add(ir_temp->result, ir_temp->data_type);
                    if (ir_temp->args1) {
                        int src_idx = get_const((char*)ir_temp->args1);
                        if (src_idx != -1 && consts->is_dynamic_array[src_idx]) {
                            temp_map_mark_dynamic(ir_temp->result);
                        }
                    }
                }
                break;
            }
            case IR_ARRAY_DECL: {
                int size = ir_temp->array_size;
                int elem_size = ir_temp->elem_size;
                int total_bytes;
                if (ir_temp->stack_bytes > 0) {
                    total_bytes = ir_temp->stack_bytes;
                } else {
                    total_bytes = size * elem_size;
                }
                if (ir_temp->is_static_global || !in_function) {
                    if (ir_temp->init_values && ir_temp->init_count > 0) {
                        GlobalArrayInit* gai = malloc(sizeof(GlobalArrayInit));
                        gai->name = strdup(ir_temp->result);
                        gai->count = ir_temp->init_count;
                        gai->values = malloc(sizeof(int) * ir_temp->init_count);
                        memcpy(gai->values, ir_temp->init_values,
                               sizeof(int) * ir_temp->init_count);
                        gai->elem_type = ir_temp->data_type;
                        gai->next = global_array_inits;
                        global_array_inits = gai;
                        add_symbol(ir_temp->data_type, "", ir_temp->result,
                                   TYPE_VAR, ir_temp->data_type,
                                   STORAGE_GLOBAL, 0, true, size,false);
                    } else {
                        add_symbol(ir_temp->data_type, "", ir_temp->result,
                                   TYPE_UNINIT_VAR, ir_temp->data_type,
                                   STORAGE_GLOBAL, 0, true, size,false);
                    }
                } else {
                    current_local_offset -= total_bytes;
                    add_symbol(ir_temp->data_type, "", ir_temp->result,
                               TYPE_VAR, ir_temp->data_type,
                               STORAGE_LOCAL, current_local_offset,
                               true, ir_temp->array_size,false);

                    if (ir_temp->init_values && ir_temp->init_count > 0) {
                        cache_flush_all(output);

                        char arr_label[64];
                        snprintf(arr_label, sizeof(arr_label),
                                 "_arrinit%d", arrinit_i++);
                        GlobalArrayInit* gai = malloc(sizeof(GlobalArrayInit));
                        gai->name   = strdup(arr_label);
                        gai->count  = ir_temp->init_count;
                        gai->values = malloc(sizeof(int) * ir_temp->init_count);
                        memcpy(gai->values, ir_temp->init_values,
                               sizeof(int) * ir_temp->init_count);
                        gai->elem_type = ir_temp->data_type;
                        gai->next = global_array_inits;
                        global_array_inits = gai;

                        fprintf(output, "  mov esi, %s\n", arr_label);
                        fprintf(output, "  lea edi, [ebp-%d]\n",
                                -current_local_offset);
                        fprintf(output, "  mov ecx, %d\n", ir_temp->init_count);
                        fprintf(output, "  cld\n");
                        fprintf(output, "  rep movsd\n");
                    }
                }
                break;
            }

            case IR_ARRAY_LOAD: {
                char* addr_temp = ir_temp->args1;
                int result_off = temp_map_get_offset(ir_temp->result);
                DataType elem_type = ir_temp->data_type;
                int elem_sz = ir_temp->elem_size;

                bool is_scalar_64 = (elem_type == DATA_INT64
                                     || elem_type == DATA_UINT64
                                     || elem_type == DATA_FLOAT64
                                     || elem_type == DATA_UFLOAT64);

                if (elem_sz > 4)
                    cache_flush_reg(output, R_EDX);

                cg_load(output, addr_temp, DATA_INT);

                if (elem_sz > 4 && !is_scalar_64) {
                    if (elem_type == DATA_STR) {
                        fprintf(output, "  mov dword [ebp-%d], eax\n", result_off);
                    } else {
                        int dwords = elem_sz / 4;
                        for (int i = 0; i < dwords; i++) {
                            fprintf(output, "  mov edx, dword [eax+%d]\n", i * 4);
                            fprintf(output, "  mov dword [ebp-%d], edx\n",
                                    result_off - i * 4);
                        }
                    }
                    if (ir_temp->result && is_temp(ir_temp->result)) {
                        temp_map_add(ir_temp->result, elem_type);
                    }
                    break;
                }

                switch (elem_type) {
                    case DATA_CHAR:
                    case DATA_BOOL:
                    case DATA_UINT8:
                        if (elem_sz == 1) {
                            fprintf(output, "  movzx eax, byte [eax]\n");
                            cg_store(output, ir_temp->result, elem_type, false);
                        } else {
                            fprintf(output, "  mov eax, dword [eax]\n");
                            cg_store(output, ir_temp->result, DATA_INT, false);
                        }
                        break;
                    case DATA_INT8:
                        if (elem_sz == 1) {
                            fprintf(output, "  movsx eax, byte [eax]\n");
                            cg_store(output, ir_temp->result, elem_type, false);
                        } else {
                            fprintf(output, "  mov eax, dword [eax]\n");
                            cg_store(output, ir_temp->result, DATA_INT, false);
                        }
                        break;
                    case DATA_UINT16:
                        if (elem_sz == 2) {
                            fprintf(output, "  movzx eax, word [eax]\n");
                            cg_store(output, ir_temp->result, elem_type, false);
                        } else {
                            fprintf(output, "  mov eax, dword [eax]\n");
                            cg_store(output, ir_temp->result, DATA_INT, false);
                        }
                        break;
                    case DATA_INT16:
                        if (elem_sz == 2) {
                            fprintf(output, "  movsx eax, word [eax]\n");
                            cg_store(output, ir_temp->result, elem_type, false);
                        } else {
                            fprintf(output, "  mov eax, dword [eax]\n");
                            cg_store(output, ir_temp->result, DATA_INT, false);
                        }
                        break;
                    case DATA_INT:
                    case DATA_UINT:
                    case DATA_STRING:
                    case DATA_IDENTIFIER:
                        fprintf(output, "  mov eax, dword [eax]\n");
                        cg_store(output, ir_temp->result, DATA_INT, false);
                        break;
                    case DATA_FLOAT:
                    case DATA_UFLOAT:
                    case DATA_FLOAT8:
                    case DATA_UFLOAT8:
                    case DATA_FLOAT16:
                    case DATA_UFLOAT16:
                    case DATA_FLOAT32:
                    case DATA_UFLOAT32:
                        if (elem_sz == 1) {
                            fprintf(output, "  movzx edx, byte [eax]\n");
                            fprintf(output, "  push edx\n");
                            fprintf(output, "  fild dword [esp]\n");
                            fprintf(output, "  add esp, 4\n");
                        } else if (elem_sz == 2) {
                            fprintf(output, "  fild word [eax]\n");
                        } else if (elem_sz == 4) {
                            fprintf(output, "  fld dword [eax]\n");
                        } else if (elem_sz == 8) {
                            fprintf(output, "  fld qword [eax]\n");
                        } else {
                            fprintf(output, "  mov eax, dword [eax]\n");
                        }
                        cg_store(output, ir_temp->result, elem_type, true);
                        break;
                    case DATA_INT64:
                    case DATA_UINT64:
                        fprintf(output, "  mov ecx, eax\n");
                        fprintf(output, "  mov eax, dword [ecx]\n");
                        fprintf(output, "  mov edx, dword [ecx+4]\n");
                        cg_store(output, ir_temp->result, elem_type, false);
                        break;
                    case DATA_FLOAT64:
                    case DATA_UFLOAT64:
                        fprintf(output, "  fld qword [eax]\n");
                        cg_store(output, ir_temp->result, elem_type, true);
                        break;
                    default:
                        fprintf(output, "  mov eax, dword [eax]\n");
                        cg_store(output, ir_temp->result, DATA_INT, false);
                        break;
                }
                if (ir_temp->result && is_temp(ir_temp->result)) {
                    temp_map_add(ir_temp->result, ir_temp->data_type);
                }

                break;
            }

            case IR_ARRAY_STORE: {
                char* addr_temp = ir_temp->args1;
                char* val_temp = ir_temp->args2;
                DataType elem_type = ir_temp->data_type;
                int elem_sz = ir_temp->elem_size;

                bool is_scalar_64 = (elem_type == DATA_INT64
                                     || elem_type == DATA_UINT64
                                     || elem_type == DATA_FLOAT64
                                     || elem_type == DATA_UFLOAT64);

                cache_flush_reg(output, R_ECX);
                if (elem_sz > 4)
                    cache_flush_reg(output, R_ESI);


                if (elem_sz > 4 && !is_scalar_64) {
                    cg_load(output, addr_temp, DATA_INT);
                    fprintf(output, "  mov ecx, eax\n");

                    int val_idx = get_const(val_temp);
                    if (val_idx != -1) {
                        const char* canon = canonical_name(val_temp);
                        switch (consts->storage[val_idx]) {
                            case STORAGE_LOCAL:
                                fprintf(output, "  lea esi, [ebp-%d]\n",
                                        -consts->offset[val_idx]);
                                break;
                            case STORAGE_PARAM:
                                fprintf(output, "  lea esi, [ebp+%d]\n",
                                        consts->offset[val_idx]);
                                break;
                            case STORAGE_GLOBAL:
                            default:
                                fprintf(output, "  mov esi, %s\n", canon);
                                break;
                        }
                    } else if (is_temp(val_temp)) {
                        int off = temp_map_get_offset(val_temp);
                        if (elem_sz == 8) {
                            fprintf(output, "  mov eax, dword [ebp-%d]\n", off);
                            fprintf(output, "  mov dword [ecx], eax\n");
                            fprintf(output, "  mov eax, dword [ebp-%d]\n", off - 4);
                            fprintf(output, "  mov dword [ecx+4], eax\n");
                            break;
                        }
                        fprintf(output, "  lea esi, [ebp-%d]\n", off);
                    } else {
                        INTERNAL_ERROR(
                            "IR_ARRAY_STORE: cannot resolve source '%s' for %d-byte copy",
                            val_temp, elem_sz);
                    }

                    int dwords = elem_sz / 4;
                    for (int i = 0; i < dwords; i++) {
                        fprintf(output, "  mov eax, dword [esi+%d]\n", i * 4);
                        fprintf(output, "  mov dword [ecx+%d], eax\n", i * 4);
                    }

                    if (elem_sz % 4 != 0) {
                        int remain = elem_sz % 4;
                        int base = dwords * 4;
                        if (remain >= 1) {
                            fprintf(output, "  mov al, byte [esi+%d]\n", base);
                            fprintf(output, "  mov byte [ecx+%d], al\n", base);
                        }
                        if (remain >= 2) {
                            fprintf(output, "  mov al, byte [esi+%d]\n", base + 1);
                            fprintf(output, "  mov byte [ecx+%d], al\n", base + 1);
                        }
                        if (remain >= 3) {
                            fprintf(output, "  mov al, byte [esi+%d]\n", base + 2);
                            fprintf(output, "  mov byte [ecx+%d], al\n", base + 2);
                        }
                    }
                    break;
                }

                cg_load(output, addr_temp, DATA_INT);
                fprintf(output, "  test eax, eax\n");
                fprintf(output, "  jz _null_ptr_error\n");
                has_bounds_check = true;
                fprintf(output, "  push eax\n");

                DataType val_loaded_type = cg_load(output, val_temp, elem_type);

                cache_flush_all(output);
                fprintf(output, "  pop ecx\n");

                switch (elem_type) {
                    case DATA_CHAR:
                    case DATA_BOOL:
                    case DATA_UINT8:
                    case DATA_INT8:
                        if (elem_sz == 1)
                            fprintf(output, "  mov byte [ecx], al\n");
                        else
                            fprintf(output, "  mov [ecx], eax\n");
                        break;
                    case DATA_INT16:
                    case DATA_UINT16:
                        if (elem_sz == 2)
                            fprintf(output, "  mov word [ecx], ax\n");
                        else
                            fprintf(output, "  mov [ecx], eax\n");
                        break;
                    case DATA_INT:
                    case DATA_UINT:
                    case DATA_STRING:
                    case DATA_IDENTIFIER:
                    case DATA_ERROR:
                        fprintf(output, "  mov [ecx], eax\n");
                        break;
                    case DATA_FLOAT:
                    case DATA_UFLOAT:
                    case DATA_FLOAT8:
                    case DATA_UFLOAT8:
                    case DATA_FLOAT16:
                    case DATA_UFLOAT16:
                    case DATA_FLOAT32:
                    case DATA_UFLOAT32:
                        if (elem_sz == 1) {
                            fprintf(output, "  sub esp, 4\n");
                            fprintf(output, "  fisttp dword [esp]\n");
                            fprintf(output, "  mov al, byte [esp]\n");
                            fprintf(output, "  add esp, 4\n");
                            fprintf(output, "  mov byte [ecx], al\n");
                        } else if (elem_sz == 2) {
                            fprintf(output, "  sub esp, 4\n");
                            fprintf(output, "  fisttp dword [esp]\n");
                            fprintf(output, "  mov ax, word [esp]\n");
                            fprintf(output, "  add esp, 4\n");
                            fprintf(output, "  mov word [ecx], ax\n");
                        } else if (elem_sz == 8) {
                            fprintf(output, "  fstp qword [ecx]\n");
                        } else {
                            fprintf(output, "  fstp dword [ecx]\n");
                        }
                        break;
                    case DATA_INT64:
                    case DATA_UINT64:
                        if (val_loaded_type != DATA_INT64 && val_loaded_type != DATA_UINT64) {
                            bool val_unsigned = (val_loaded_type == DATA_UINT  ||
                                                 val_loaded_type == DATA_UINT8  ||
                                                 val_loaded_type == DATA_UINT16 ||
                                                 val_loaded_type == DATA_UINT32);
                            if (val_unsigned) {
                                fprintf(output, "  xor edx, edx\n");
                            } else {
                                fprintf(output, "  cdq\n");
                            }
                        }
                        fprintf(output, "  mov [ecx], eax\n");
                        fprintf(output, "  mov [ecx+4], edx\n");
                        break;
                    case DATA_FLOAT64:
                    case DATA_UFLOAT64:
                        fprintf(output, "  fstp qword [ecx]\n");
                        break;
                    default:
                        fprintf(output, "  mov [ecx], eax\n");
                        break;
                }
                break;
            }

            case IR_ALLOC_ARRAY: {
                cache_flush_all(output);
                if (ir_temp->stack_bytes > 0) {
                    if (ir_temp->result != NULL) {
                        int alloc_bytes = (ir_temp->stack_bytes + 3) & ~3;
                        current_local_offset -= alloc_bytes;
                        fprintf(output, "  ; stack temp at [ebp-%d]\n", -current_local_offset);
                        fprintf(output, "  lea eax, [ebp-%d]\n", -current_local_offset);
                        cg_store(output, ir_temp->result, DATA_INT, false);
                    }
                    has_memory_lib = true;
                    break;
                }
                char* size_val = ir_temp->args1;
                int elem_size = ir_temp->elem_size;

                if (is_number(size_val)) {
                    fprintf(output, "  mov ebx, %s\n", size_val);
                } else if (is_temp(size_val)) {
                    int size_offset = temp_map_get_offset(size_val);
                    fprintf(output, "  mov ebx, [ebp-%d]\n", size_offset);
                } else {
                    cg_load(output, size_val, DATA_INT);
                    fprintf(output, "  mov ebx, eax\n");
                }
                fprintf(output, "  mov eax, ebx\n");
                fprintf(output, "  mov ecx, %d\n", elem_size);
                fprintf(output, "  imul eax, ecx\n");
                fprintf(output, "  add eax, 4\n");
                fprintf(output, "  push eax\n");
                fprintf(output, "  call _gc_alloc\n");
                fprintf(output, "  add esp, 4\n");
                fprintf(output, "  push eax\n");
                cache_flush_all(output);
                fprintf(output, "  pop edx\n");
                fprintf(output, "  mov [edx], ebx\n");
                fprintf(output, "  lea eax, [edx + 4]\n");
                fprintf(output, "  push eax\n");
                fprintf(output, "  mov ecx, ebx\n");
                fprintf(output, "  mov eax, %d\n", elem_size);
                fprintf(output, "  imul ecx, eax\n");
                fprintf(output, "  mov edi, [esp]\n");
                fprintf(output, "  xor eax, eax\n");
                fprintf(output, "  cld\n");
                fprintf(output, "  rep stosb\n");
                fprintf(output, "  pop eax\n");
                if (ir_temp->result && ir_temp->result[0]) {
                    cg_store(output, ir_temp->result, DATA_INT, false);
                }
                has_memory_lib = true;
                has_gc = true;
                break;
            }

            case IR_ARRAY_LENGTH: {
                cache_flush_all(output);
                char* arr_name = ir_temp->args1;
                int idx = get_const(arr_name);
                char addr_buf[100];
                if (idx == -1) {
                    FATAL(ERR_CG_UNKNOWN_VAR, LOC_NONE, "array '%s' not found", arr_name);
                }
                if (consts->is_dynamic_array[idx]) {
                    if (consts->storage[idx] == STORAGE_PARAM) {
                        if (main_has_args && current_function
                            && strcmp(current_function, "_main") == 0) {
                            fprintf(output, "  mov eax, dword [ebp+%d]\n",
                                    consts->offset[idx]);
                            fprintf(output, "  mov eax, dword [eax-4]\n");
                        } else {
                            fprintf(output, "  mov eax, dword [ebp+%d]\n",
                                    consts->offset[idx] + 4);
                        }
                    } else {
                        cg_address(output, arr_name, addr_buf, sizeof(addr_buf));
                        fprintf(output, "  mov eax, dword %s\n", addr_buf);
                        fprintf(output, "  mov eax, dword [eax-4]\n");
                    }
                    has_memory_lib = true;
                }
                else if (consts->is_array[idx]) {
                    int total_size = consts->array_size[idx];
                    fprintf(output, "  mov eax, %d\n", total_size);
                } else {
                    cg_address(output, arr_name, addr_buf, sizeof(addr_buf));
                    fprintf(output, "  mov eax, dword %s\n", addr_buf);
                    fprintf(output, "  mov eax, dword [eax-4]\n");
                }
                cg_store(output, ir_temp->result, DATA_INT, false);
                has_memory_lib = true;
                break;
            }

            case IR_ARRAY_TOSTRING: {
                cache_flush_all(output);
                has_array_lib = true;
                has_memory_lib = true;
                has_print = true;
                int type_code = get_type_code(ir_temp->args1_type);

                char* arr_name = ir_temp->args1;
                int arr_idx = get_const(arr_name);

                char var_addr[100] = {0};
                if (arr_idx != -1) {
                    switch (consts->storage[arr_idx]) {
                    case STORAGE_LOCAL:
                        snprintf(var_addr, sizeof(var_addr), "[ebp-%d]",
                                 -consts->offset[arr_idx]);
                        break;
                    case STORAGE_PARAM:
                        snprintf(var_addr, sizeof(var_addr), "[ebp+%d]",
                                 consts->offset[arr_idx]);
                        break;
                    default:
                        snprintf(var_addr, sizeof(var_addr), "[%s]",
                                 consts->name[arr_idx] ? consts->name[arr_idx] : arr_name);
                        break;
                    }
                }

                if (arr_idx != -1 && consts->is_dynamic_array[arr_idx]) {
                    size_t alen = strlen(var_addr);
                    var_addr[alen - 1] = '\0';
                    fprintf(output, "  mov eax, dword %s]\n", var_addr);
                    fprintf(output, "  push %d\n", type_code);
                    fprintf(output, "  push dword %s+4]\n", var_addr);
                    fprintf(output, "  push eax\n");
                    fprintf(output, "  call _array_to_string\n");
                    fprintf(output, "  add esp, 12\n");
                    cache_flush_all(output);
                    cg_store(output, ir_temp->result, DATA_STRING, false);
                    has_string_lib = true;
                    has_gc = true;
                    break;
                }

                if (is_temp(arr_name)) {
                    int base_off = temp_map_get_offset(arr_name);
                    fprintf(output, "  mov eax, [ebp-%d]\n", base_off);
                } else if (arr_idx != -1) {
                    fprintf(output, "  lea eax, %s\n", var_addr);
                } else {
                    fprintf(output, "  mov eax, %s\n", arr_name);
                }

                if (is_number(ir_temp->args2)) {
                    int len = atoi(ir_temp->args2);
                    fprintf(output, "  push %d\n", type_code);
                    fprintf(output, "  push %d\n", len);
                } else {
                    int len_idx = get_const(ir_temp->args2);
                    if (len_idx != -1 && consts->is_array[len_idx]
                        && !consts->is_dynamic_array[len_idx]) {
                        fprintf(output, "  push %d\n", type_code);
                        fprintf(output, "  push %d\n", consts->array_size[len_idx]);
                    } else {
                        fprintf(output, "  push %d\n", type_code);
                        fprintf(output, "  push dword [eax - 4]\n");
                    }
                }
                fprintf(output, "  push eax\n");
                fprintf(output, "  call _array_to_string\n");
                fprintf(output, "  add esp, 12\n");
                cache_flush_all(output);
                cg_store(output, ir_temp->result, DATA_STRING, false);
                has_string_lib = true;
                has_gc = true;
                break;
            }

            case IR_PUSH: {
                cache_flush_all(output);
                char* operand = ir_temp->args1;
                int push_bytes = ir_temp->push_bytes;
                if (push_bytes <= 0) push_bytes = 4;
                if ((ir_temp->args1_type == DATA_INT64 || ir_temp->args1_type == DATA_UINT64)
                    && push_bytes < 8) {
                    push_bytes = 8;
                }

                if (push_bytes <= 4) {
                    if (is_temp(operand)) {
                        int offset = temp_map_get_offset(operand);
                        fprintf(output, "  push dword [ebp-%d]\n", offset);
                    } else {
                        int idx = get_const(operand);
                        if (idx != -1) {
                            char addr[100];
                            switch (consts->storage[idx]) {
                            case STORAGE_GLOBAL:
                                snprintf(addr, sizeof(addr), "[%s]",
                                         consts->name[idx]
                                             ? consts->name[idx] : operand);
                                break;
                            case STORAGE_PARAM:
                                snprintf(addr, sizeof(addr), "[ebp+%d]",
                                         consts->offset[idx]);
                                break;
                            case STORAGE_LOCAL:
                                snprintf(addr, sizeof(addr), "[ebp-%d]",
                                         -consts->offset[idx]);
                                break;
                            default:
                                snprintf(addr, sizeof(addr), "[%s]", operand);
                                break;
                            }
                            if (consts->is_dynamic_array[idx]) {
                                fprintf(output, "  mov eax, dword %s\n", addr);
                                fprintf(output, "  push eax\n");
                                break;
                            }
                            if (consts->type[idx] == DATA_CHAR ||
                                consts->type[idx] == DATA_BOOL) {
                                fprintf(output, "  movzx eax, byte %s\n", addr);
                            } else if (consts->type[idx] == DATA_INT16) {
                                fprintf(output, "  movsx eax, word %s\n", addr);
                            } else if (consts->type[idx] == DATA_UINT16) {
                                fprintf(output, "  movzx eax, word %s\n", addr);
                            } else {
                                fprintf(output, "  mov eax, dword %s\n", addr);
                            }
                            fprintf(output, "  push eax\n");
                        } else if ((ir_temp->args1_type == DATA_FLOAT
                                    || ir_temp->args1_type == DATA_UFLOAT)
                                   && is_number(operand)) {
                            char* flabel = add_const(DATA_FLOAT, operand, 0, DATA_FLOAT);
                            fprintf(output, "  push dword [%s]\n", flabel);
                        } else {
                            fprintf(output, "  mov eax, %s\n", operand);
                            fprintf(output, "  push eax\n");
                        }
                    }
                    break;
                }

                if (push_bytes == 8 && !ir_temp->is_str_param) {
                    int op_sz = get_type_size(ir_temp->args1_type);
                    if (op_sz < 8) {
                        if (is_temp(operand)) {
                            int off = temp_map_get_offset(operand);
                            if (temp_map_is_dynamic(operand)) {
                                fprintf(output, "  mov eax, dword [ebp-%d]\n", off);
                                fprintf(output, "  mov edx, dword [eax-4]\n");
                                fprintf(output, "  push edx\n");
                                fprintf(output, "  push eax\n");
                                break;
                            }
                            if (temp_map_get_array_size(operand) > 0) {
                                fprintf(output, "  mov eax, dword [ebp-%d]\n", off);
                                fprintf(output, "  push %d\n",
                                        temp_map_get_array_size(operand));
                                fprintf(output, "  push eax\n");
                                break;
                            }
                            fprintf(output, "  mov eax, dword [ebp-%d]\n", off);
                        } else if (is_number((char*)operand)) {
                            fprintf(output, "  mov eax, %s\n", operand);
                        } else {
                            int idx = get_const(operand);
                            if (idx != -1) {
                                char addr[100];
                                switch (consts->storage[idx]) {
                                    case STORAGE_GLOBAL:
                                        snprintf(addr, sizeof(addr), "[%s]",
                                                 consts->name[idx]
                                                     ? consts->name[idx] : operand);
                                        break;
                                    case STORAGE_PARAM:
                                        snprintf(addr, sizeof(addr), "[ebp+%d]",
                                                 consts->offset[idx]);
                                        break;
                                    case STORAGE_LOCAL:
                                        snprintf(addr, sizeof(addr), "[ebp-%d]",
                                                 -consts->offset[idx]);
                                        break;
                                    default:
                                        snprintf(addr, sizeof(addr), "[%s]", operand);
                                        break;
                                }
                                if (consts->is_dynamic_array[idx]) {
                                    fprintf(output, "  mov eax, dword %s\n", addr);
                                    fprintf(output, "  mov edx, dword [eax-4]\n");
                                    fprintf(output, "  push edx\n");
                                    fprintf(output, "  push eax\n");
                                    break;
                                }
                                if (consts->is_array[idx]) {
                                    if (consts->storage[idx] == STORAGE_PARAM) {
                                        fprintf(output, "  mov eax, dword %s\n", addr);
                                    } else {
                                        fprintf(output, "  lea eax, %s\n", addr);
                                    }
                                    fprintf(output, "  push %d\n", consts->array_size[idx]);
                                    fprintf(output, "  push eax\n");
                                    break;
                                }
                                if (consts->type[idx] == DATA_CHAR ||
                                    consts->type[idx] == DATA_BOOL) {
                                    fprintf(output, "  movzx eax, byte %s\n", addr);
                                } else if (consts->type[idx] == DATA_INT16) {
                                    fprintf(output, "  movsx eax, word %s\n", addr);
                                } else if (consts->type[idx] == DATA_UINT16) {
                                    fprintf(output, "  movzx eax, word %s\n", addr);
                                } else {
                                    fprintf(output, "  mov eax, dword %s\n", addr);
                                }
                            } else {
                                fprintf(output, "  mov eax, %s\n", operand);
                            }
                        }
                        if (ir_temp->args1_type == DATA_UINT ||
                            ir_temp->args1_type == DATA_UINT32 ||
                            ir_temp->args1_type == DATA_UINT8 ||
                            ir_temp->args1_type == DATA_UINT16) {
                            fprintf(output, "  xor edx, edx\n");
                        } else {
                            fprintf(output, "  cdq\n");
                        }
                        fprintf(output, "  push edx\n");
                        fprintf(output, "  push eax\n");
                        break;
                    }
                    if (is_temp(operand)) {
                        int off = temp_map_get_offset(operand);
                        fprintf(output, "  push dword [ebp-%d]\n", off - 4);
                        fprintf(output, "  push dword [ebp-%d]\n", off);
                    } else if (is_number((char*)operand)) {
                        long long v = parse_integer64(operand);
                        fprintf(output, "  push %u\n",
                                (unsigned)((v >> 32) & 0xFFFFFFFFLL));
                        fprintf(output, "  push %u\n",
                                (unsigned)(v & 0xFFFFFFFFLL));
                    } else {
                        int idx = get_const(operand);
                        if (idx != -1) {
                            char lo[100], hi[100];
                            switch (consts->storage[idx]) {
                            case STORAGE_PARAM:
                                snprintf(lo, sizeof(lo), "[ebp+%d]",
                                         consts->offset[idx]);
                                snprintf(hi, sizeof(hi), "[ebp+%d]",
                                         consts->offset[idx] + 4);
                                break;
                            case STORAGE_LOCAL:
                                snprintf(lo, sizeof(lo), "[ebp-%d]",
                                         -consts->offset[idx]);
                                snprintf(hi, sizeof(hi), "[ebp-%d]",
                                         -consts->offset[idx] - 4);
                                break;
                            case STORAGE_GLOBAL:
                            default:
                                snprintf(lo, sizeof(lo), "[%s]",
                                         consts->name[idx]
                                             ? consts->name[idx] : operand);
                                snprintf(hi, sizeof(hi), "[%s+4]",
                                         consts->name[idx]
                                             ? consts->name[idx] : operand);
                                break;
                            }
                            fprintf(output, "  push dword %s\n", hi);
                            fprintf(output, "  push dword %s\n", lo);
                        } else {
                            fprintf(output, "  push dword [%s+4]\n", operand);
                            fprintf(output, "  push dword [%s]\n", operand);
                        }
                    }
                    break;
                }

                int dwords = push_bytes / 4;
                fprintf(output, "  sub esp, %d\n", push_bytes);
                if (is_temp(operand)) {
                    int offset = temp_map_get_offset(operand);
                    fprintf(output, "  lea esi, [ebp-%d]\n", offset);
                } else if (ir_temp->is_str_param) {
                    char str_addr[100];
                    if (cg_address(output, operand, str_addr, sizeof(str_addr))) {
                        fprintf(output, "  lea esi, %s\n", str_addr);
                    } else {
                        cg_load(output, operand, DATA_INT);
                        fprintf(output, "  mov esi, eax\n");
                    }
                } else {
                    int idx = get_const(operand);
                    if (idx != -1) {
                        switch (consts->storage[idx]) {
                            case STORAGE_LOCAL:
                                fprintf(output, "  lea esi, [ebp-%d]\n", -consts->offset[idx]);
                                break;
                            case STORAGE_PARAM:
                                fprintf(output, "  lea esi, [ebp+%d]\n", consts->offset[idx]);
                                break;
                            default:
                                fprintf(output, "  lea esi, [%s]\n", operand);
                                break;
                        }
                    } else {
                        fprintf(output, "  mov esi, %s\n", operand);
                    }
                }
                fprintf(output, "  mov edi, esp\n");
                fprintf(output, "  mov ecx, %d\n", dwords);
                fprintf(output, "  cld\n");
                fprintf(output, "  rep movsd\n");
                break;
            }

            case IR_BINARY: {
                DataType res_type = ir_temp->data_type;
                if (ir_temp->op == IR_JE || ir_temp->op == IR_JNE ||
                        ir_temp->op == IR_JG || ir_temp->op == IR_JL ||
                        ir_temp->op == IR_JGE || ir_temp->op == IR_JLE ||
                        ir_temp->op == IR_ULT || ir_temp->op == IR_ULE ||
                        ir_temp->op == IR_UGT || ir_temp->op == IR_UGE) {
                    res_type = DATA_INT;
                        }
                temp_map_add(ir_temp->result, res_type);

                int lhs_sz = get_type_size(ir_temp->args1_type);
                int rhs_sz = get_type_size(ir_temp->args2_type);
                int res_sz = get_type_size(res_type);
                bool is_64bit = (lhs_sz == 8 || rhs_sz == 8 || res_sz == 8);
                bool is_signed = (ir_temp->args1_type != DATA_UINT64 &&
                                  ir_temp->args2_type != DATA_UINT64);
                if (is_64bit && !(ir_temp->data_type == DATA_FLOAT64 ||
                                    ir_temp->data_type == DATA_UFLOAT64)) {
                    cache_flush_all(output);
                    char addr1_l[100] = {0}, addr1_h[100] = {0};
                    char addr2_l[100] = {0}, addr2_h[100] = {0};

                    int idx1 = get_const(ir_temp->args1);
                    if (idx1 >= 0) {
                        switch (consts->storage[idx1]) {
                            case STORAGE_GLOBAL:
                                snprintf(addr1_l, sizeof(addr1_l), "[%s]", consts->name[idx1]);
                                snprintf(addr1_h, sizeof(addr1_h), "[%s+4]", consts->name[idx1]);
                                break;
                            case STORAGE_PARAM:
                                snprintf(addr1_l, sizeof(addr1_l), "[ebp+%d]", consts->offset[idx1]);
                                snprintf(addr1_h, sizeof(addr1_h), "[ebp+%d]", consts->offset[idx1] + 4);
                                break;
                            case STORAGE_LOCAL:
                                snprintf(addr1_l, sizeof(addr1_l), "[ebp-%d]", -consts->offset[idx1]);
                                snprintf(addr1_h, sizeof(addr1_h), "[ebp-%d]", -consts->offset[idx1] - 4);
                                break;
                            default:
                                snprintf(addr1_l, sizeof(addr1_l), "[%s]", ir_temp->args1);
                                snprintf(addr1_h, sizeof(addr1_h), "[%s+4]", ir_temp->args1);
                                break;
                        }
                    } else if (is_temp(ir_temp->args1)) {
                        int off = temp_map_get_offset(ir_temp->args1);
                        snprintf(addr1_l, sizeof(addr1_l), "[ebp-%d]", off);
                        snprintf(addr1_h, sizeof(addr1_h), "[ebp-%d]", off - 4);
                    } else if (is_number((char*)ir_temp->args1)) {
                        long long v = parse_integer64(ir_temp->args1);
                        snprintf(addr1_l, sizeof(addr1_l), "%u",
                                 (unsigned)(v & 0xFFFFFFFFLL));
                        snprintf(addr1_h, sizeof(addr1_h), "%u",
                                 (unsigned)((v >> 32) & 0xFFFFFFFFLL));
                    } else {
                        snprintf(addr1_l, sizeof(addr1_l), "[%s]", ir_temp->args1);
                        snprintf(addr1_h, sizeof(addr1_h), "[%s+4]", ir_temp->args1);
                    }

                    int idx2 = get_const(ir_temp->args2);
                    if (idx2 >= 0) {
                        switch (consts->storage[idx2]) {
                            case STORAGE_GLOBAL:
                                snprintf(addr2_l, sizeof(addr2_l), "[%s]", consts->name[idx2]);
                                snprintf(addr2_h, sizeof(addr2_h), "[%s+4]", consts->name[idx2]);
                                break;
                            case STORAGE_PARAM:
                                snprintf(addr2_l, sizeof(addr2_l), "[ebp+%d]", consts->offset[idx2]);
                                snprintf(addr2_h, sizeof(addr2_h), "[ebp+%d]", consts->offset[idx2] + 4);
                                break;
                            case STORAGE_LOCAL:
                                snprintf(addr2_l, sizeof(addr2_l), "[ebp-%d]", -consts->offset[idx2]);
                                snprintf(addr2_h, sizeof(addr2_h), "[ebp-%d]", -consts->offset[idx2] - 4);
                                break;
                            default:
                                snprintf(addr2_l, sizeof(addr2_l), "[%s]", ir_temp->args2);
                                snprintf(addr2_h, sizeof(addr2_h), "[%s+4]", ir_temp->args2);
                                break;
                        }
                    } else if (is_temp(ir_temp->args2)) {
                        int off2 = temp_map_get_offset(ir_temp->args2);
                        snprintf(addr2_l, sizeof(addr2_l), "[ebp-%d]", off2);
                        snprintf(addr2_h, sizeof(addr2_h), "[ebp-%d]", off2 - 4);
                    } else if (is_number((char*)ir_temp->args2)) {
                        long long v = parse_integer64(ir_temp->args2);
                        snprintf(addr2_l, sizeof(addr2_l), "%u",
                                 (unsigned)(v & 0xFFFFFFFFLL));
                        snprintf(addr2_h, sizeof(addr2_h), "%u",
                                 (unsigned)((v >> 32) & 0xFFFFFFFFLL));
                    } else {
                        snprintf(addr2_l, sizeof(addr2_l), "[%s]", ir_temp->args2);
                        snprintf(addr2_h, sizeof(addr2_h), "[%s+4]", ir_temp->args2);
                    }

                    bool is_unsigned64 = (ir_temp->data_type == DATA_UINT64 ||
                                          ir_temp->args1_type == DATA_UINT64 ||
                                          ir_temp->args2_type == DATA_UINT64);

                    bool lhs_unsigned32 = (ir_temp->args1_type == DATA_UINT  ||
                                           ir_temp->args1_type == DATA_UINT8  ||
                                           ir_temp->args1_type == DATA_UINT16 ||
                                           ir_temp->args1_type == DATA_UINT32);
                    bool rhs_unsigned32 = (ir_temp->args2_type == DATA_UINT  ||
                                           ir_temp->args2_type == DATA_UINT8  ||
                                           ir_temp->args2_type == DATA_UINT16 ||
                                           ir_temp->args2_type == DATA_UINT32);

                    if (lhs_sz >= 8) {
                        fprintf(output, "  mov eax, dword %s\n", addr1_l);
                        fprintf(output, "  mov edx, dword %s\n", addr1_h);
                    } else {
                        fprintf(output, "  mov eax, dword %s\n", addr1_l);
                        fprintf(output, (is_unsigned64 || lhs_unsigned32)
                                         ? "  xor edx, edx\n" : "  cdq\n");
                    }

                    if (rhs_sz >= 8) {
                        fprintf(output, "  mov ebx, dword %s\n", addr2_l);
                        fprintf(output, "  mov ecx, dword %s\n", addr2_h);
                    } else {
                        fprintf(output, "  mov ebx, dword %s\n", addr2_l);
                        if (is_unsigned64 || rhs_unsigned32) {
                            fprintf(output, "  xor ecx, ecx\n");
                        } else {
                            fprintf(output, "  mov ecx, ebx\n");
                            fprintf(output, "  sar ecx, 31\n");
                        }
                    }

                    switch (ir_temp->op) {
                        case IR_ADD:
                            fprintf(output, "  add eax, ebx\n");
                            fprintf(output, "  adc edx, ecx\n");
                            break;
                        case IR_SUB:
                            fprintf(output, "  sub eax, ebx\n");
                            fprintf(output, "  sbb edx, ecx\n");
                            break;

                        case IR_MUL:
                            if (is_unsigned64) {
                                fprintf(output, "  push ecx\n");
                                fprintf(output, "  push ebx\n");
                                fprintf(output, "  push eax\n");
                                fprintf(output, "  mul ebx\n");
                                fprintf(output, "  mov ebx, eax\n");
                                fprintf(output, "  mov ecx, edx\n");
                                fprintf(output, "  pop eax\n");
                                fprintf(output, "  mul dword [esp+4]\n");
                                fprintf(output, "  add ecx, eax\n");
                                fprintf(output, "  pop eax\n");
                                fprintf(output, "  mul edx\n");
                                fprintf(output, "  add ecx, eax\n");
                                fprintf(output, "  mov eax, ebx\n");
                                fprintf(output, "  mov edx, ecx\n");
                                fprintf(output, "  pop ecx\n");
                            } else {
                                fprintf(output, "  push ecx\n");
                                fprintf(output, "  push ebx\n");
                                fprintf(output, "  push eax\n");
                                fprintf(output, "  imul ebx\n");
                                fprintf(output, "  mov ebx, eax\n");
                                fprintf(output, "  mov ecx, edx\n");
                                fprintf(output, "  pop eax\n");
                                fprintf(output, "  imul dword [esp+4]\n");
                                fprintf(output, "  add ecx, eax\n");
                                fprintf(output, "  pop eax\n");
                                fprintf(output, "  imul edx\n");
                                fprintf(output, "  add ecx, eax\n");
                                fprintf(output, "  mov eax, ebx\n");
                                fprintf(output, "  mov edx, ecx\n");
                                fprintf(output, "  pop ecx\n");
                            }
                            break;

                        case IR_DIV:
                            has_64bit_ops = true;
                            has_gc = true;
                            fprintf(output, "  call _div64\n");
                            break;
                        case IR_MOD:
                            has_64bit_ops = true;
                            has_gc = true;
                            fprintf(output, "  call _mod64\n");
                            break;
                        case IR_UDIV:
                            has_64bit_ops = true;
                            has_gc = true;
                            fprintf(output, "  call _udiv64\n");
                            break;
                        case IR_UMOD:
                            has_64bit_ops = true;
                            has_gc = true;
                            fprintf(output, "  call _umod64\n");
                            break;

                        case IR_JE:
                            fprintf(output, "  cmp edx, ecx\n");
                            fprintf(output, "  jne .L_set0_%d\n", cg64_label_counter);
                            fprintf(output, "  cmp eax, ebx\n");
                            fprintf(output, "  jne .L_set0_%d\n", cg64_label_counter);
                            fprintf(output, "  mov eax, 1\n");
                            fprintf(output, "  jmp .L_c64end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set0_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, ".L_c64end_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor edx, edx\n");
                            cg64_label_counter++;
                            break;
                        case IR_JNE:
                            fprintf(output, "  cmp edx, ecx\n");
                            fprintf(output, "  jne .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, "  cmp eax, ebx\n");
                            fprintf(output, "  jne .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, "  jmp .L_c64end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set1_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, 1\n");
                            fprintf(output, ".L_c64end_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor edx, edx\n");
                            cg64_label_counter++;
                            break;
                        case IR_JL:
                            fprintf(output, "  cmp edx, ecx\n");
                            fprintf(output, "  jl .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, "  jg .L_set0_%d\n", cg64_label_counter);
                            fprintf(output, "  cmp eax, ebx\n");
                            fprintf(output, "  jb .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set0_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, "  jmp .L_c64end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set1_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, 1\n");
                            fprintf(output, ".L_c64end_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor edx, edx\n");
                            cg64_label_counter++;
                            break;
                        case IR_JG:
                            fprintf(output, "  cmp edx, ecx\n");
                            fprintf(output, "  jg .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, "  jl .L_set0_%d\n", cg64_label_counter);
                            fprintf(output, "  cmp eax, ebx\n");
                            fprintf(output, "  ja .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set0_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, "  jmp .L_c64end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set1_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, 1\n");
                            fprintf(output, ".L_c64end_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor edx, edx\n");
                            cg64_label_counter++;
                            break;
                        case IR_JLE:
                            fprintf(output, "  cmp edx, ecx\n");
                            fprintf(output, "  jl .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, "  jg .L_set0_%d\n", cg64_label_counter);
                            fprintf(output, "  cmp eax, ebx\n");
                            fprintf(output, "  jbe .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set0_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, "  jmp .L_c64end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set1_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, 1\n");
                            fprintf(output, ".L_c64end_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor edx, edx\n");
                            cg64_label_counter++;
                            break;
                        case IR_JGE:
                            fprintf(output, "  cmp edx, ecx\n");
                            fprintf(output, "  jg .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, "  jl .L_set0_%d\n", cg64_label_counter);
                            fprintf(output, "  cmp eax, ebx\n");
                            fprintf(output, "  jae .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set0_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, "  jmp .L_c64end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set1_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, 1\n");
                            fprintf(output, ".L_c64end_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor edx, edx\n");
                            cg64_label_counter++;
                            break;

                        case IR_ULT:
                            fprintf(output, "  cmp edx, ecx\n");
                            fprintf(output, "  jb .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, "  ja .L_set0_%d\n", cg64_label_counter);
                            fprintf(output, "  cmp eax, ebx\n");
                            fprintf(output, "  jb .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set0_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, "  jmp .L_c64end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set1_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, 1\n");
                            fprintf(output, ".L_c64end_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor edx, edx\n");
                            cg64_label_counter++;
                            break;
                        case IR_ULE:
                            fprintf(output, "  cmp edx, ecx\n");
                            fprintf(output, "  jb .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, "  ja .L_set0_%d\n", cg64_label_counter);
                            fprintf(output, "  cmp eax, ebx\n");
                            fprintf(output, "  jbe .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set0_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, "  jmp .L_c64end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set1_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, 1\n");
                            fprintf(output, ".L_c64end_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor edx, edx\n");
                            cg64_label_counter++;
                            break;
                        case IR_UGT:
                            fprintf(output, "  cmp edx, ecx\n");
                            fprintf(output, "  ja .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, "  jb .L_set0_%d\n", cg64_label_counter);
                            fprintf(output, "  cmp eax, ebx\n");
                            fprintf(output, "  ja .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set0_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, "  jmp .L_c64end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set1_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, 1\n");
                            fprintf(output, ".L_c64end_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor edx, edx\n");
                            cg64_label_counter++;
                            break;
                        case IR_UGE:
                            fprintf(output, "  cmp edx, ecx\n");
                            fprintf(output, "  ja .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, "  jb .L_set0_%d\n", cg64_label_counter);
                            fprintf(output, "  cmp eax, ebx\n");
                            fprintf(output, "  jae .L_set1_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set0_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, "  jmp .L_c64end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_set1_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, 1\n");
                            fprintf(output, ".L_c64end_%d:\n", cg64_label_counter);
                            fprintf(output, "  xor edx, edx\n");
                            cg64_label_counter++;
                            break;

                        case IR_BITWISE_AND:
                            fprintf(output, "  and eax, ebx\n");
                            fprintf(output, "  and edx, ecx\n");
                            break;
                        case IR_BITWISE_OR:
                            fprintf(output, "  or eax, ebx\n");
                            fprintf(output, "  or edx, ecx\n");
                            break;
                        case IR_BITWISE_XOR:
                            fprintf(output, "  xor eax, ebx\n");
                            fprintf(output, "  xor edx, ecx\n");
                            break;

                        case IR_SHL:
                            fprintf(output, "  mov ecx, ebx\n");
                            fprintf(output, "  cmp ebx, 32\n");
                            fprintf(output, "  jae .L_shl64_ge32_%d\n", cg64_label_counter);
                            fprintf(output, "  shld edx, eax, cl\n");
                            fprintf(output, "  shl eax, cl\n");
                            fprintf(output, "  jmp .L_shl64_end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_shl64_ge32_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov edx, eax\n");
                            fprintf(output, "  shl edx, cl\n");
                            fprintf(output, "  xor eax, eax\n");
                            fprintf(output, ".L_shl64_end_%d:\n", cg64_label_counter);
                            cg64_label_counter++;
                            break;
                        case IR_SHR:
                            fprintf(output, "  mov ecx, ebx\n");
                            fprintf(output, "  cmp ebx, 32\n");
                            fprintf(output, "  jae .L_shr64_ge32_%d\n", cg64_label_counter);
                            fprintf(output, "  shrd eax, edx, cl\n");
                            fprintf(output, "  shr edx, cl\n");
                            fprintf(output, "  jmp .L_shr64_end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_shr64_ge32_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, edx\n");
                            fprintf(output, "  shr eax, cl\n");
                            fprintf(output, "  xor edx, edx\n");
                            fprintf(output, ".L_shr64_end_%d:\n", cg64_label_counter);
                            cg64_label_counter++;
                            break;
                        case IR_SAR:
                            fprintf(output, "  mov ecx, ebx\n");
                            fprintf(output, "  cmp ebx, 32\n");
                            fprintf(output, "  jae .L_sar64_ge32_%d\n", cg64_label_counter);
                            fprintf(output, "  shrd eax, edx, cl\n");
                            fprintf(output, "  sar edx, cl\n");
                            fprintf(output, "  jmp .L_sar64_end_%d\n", cg64_label_counter);
                            fprintf(output, ".L_sar64_ge32_%d:\n", cg64_label_counter);
                            fprintf(output, "  mov eax, edx\n");
                            fprintf(output, "  sar eax, cl\n");
                            fprintf(output, "  mov edx, eax\n");
                            fprintf(output, "  sar edx, 31\n");
                            fprintf(output, ".L_sar64_end_%d:\n", cg64_label_counter);
                            cg64_label_counter++;
                            break;

                        default:
                            fprintf(output, "  ; WARNING: unsupported 64-bit op type %d, using 32-bit\n", ir_temp->op);
                            fprintf(output, "  add eax, ebx\n");
                            break;
                    }

                    char raddr_l[100];
                    int ridx = get_const(ir_temp->result);
                    if (ridx >= 0) {
                        switch (consts->storage[ridx]) {
                            case STORAGE_LOCAL:
                                snprintf(raddr_l, sizeof(raddr_l), "[ebp-%d]", -consts->offset[ridx]);
                                break;
                            case STORAGE_PARAM:
                                snprintf(raddr_l, sizeof(raddr_l), "[ebp+%d]", consts->offset[ridx]);
                                break;
                            case STORAGE_GLOBAL:
                                snprintf(raddr_l, sizeof(raddr_l), "[%s]", consts->name[ridx]);
                                break;
                            default:
                                snprintf(raddr_l, sizeof(raddr_l), "[%s]", ir_temp->result);
                                break;
                        }
                    } else if (is_temp(ir_temp->result)) {
                        int roff = temp_map_get_offset(ir_temp->result);
                        snprintf(raddr_l, sizeof(raddr_l), "[ebp-%d]", roff);
                    } else {
                        snprintf(raddr_l, sizeof(raddr_l), "[%s]", ir_temp->result);
                    }
                    int ridx2 = get_const(ir_temp->result);
                    if (ridx2 >= 0) {
                        char raddr_h[100];
                        switch (consts->storage[ridx2]) {
                            case STORAGE_LOCAL:
                                snprintf(raddr_h, sizeof(raddr_h), "[ebp-%d]",
                                         -consts->offset[ridx2] - 4);
                                break;
                            case STORAGE_PARAM:
                                snprintf(raddr_h, sizeof(raddr_h), "[ebp+%d]",
                                         consts->offset[ridx2] + 4);
                                break;
                            case STORAGE_GLOBAL:
                            default:
                                snprintf(raddr_h, sizeof(raddr_h), "[%s+4]",
                                         consts->name[ridx2]);
                                break;
                        }
                        fprintf(output, "  mov dword %s, eax\n", raddr_l);
                        fprintf(output, "  mov dword %s, edx\n", raddr_h);
                    } else if (is_temp(ir_temp->result)) {
                        int roff = temp_map_get_offset(ir_temp->result);
                        fprintf(output, "  mov dword [ebp-%d], eax\n", roff);
                        fprintf(output, "  mov dword [ebp-%d], edx\n", roff - 4);
                    } else {
                        fprintf(output, "  mov dword %s, eax\n", raddr_l);
                        fprintf(output, "  ; WARNING: cannot compute high dword for %s\n",
                                ir_temp->result);
                    }
                    break;
                                  }

                bool is_float_op = (res_type == DATA_FLOAT || res_type == DATA_UFLOAT ||
                                    res_type == DATA_FLOAT64 || res_type == DATA_UFLOAT64 ||
                                    res_type == DATA_FLOAT8 || res_type == DATA_FLOAT16 ||
                                    res_type == DATA_FLOAT32 || res_type == DATA_FLOAT128 ||
                                    res_type == DATA_UFLOAT8 || res_type == DATA_UFLOAT16 ||
                                    res_type == DATA_UFLOAT32 || res_type == DATA_UFLOAT128) ||
                                   (ir_temp->args1_type == DATA_FLOAT || ir_temp->args1_type == DATA_UFLOAT ||
                                    ir_temp->args1_type == DATA_FLOAT64 || ir_temp->args1_type == DATA_UFLOAT64 ||
                                    ir_temp->args1_type == DATA_FLOAT8 || ir_temp->args1_type == DATA_FLOAT16 ||
                                    ir_temp->args1_type == DATA_FLOAT32 || ir_temp->args1_type == DATA_FLOAT128 ||
                                    ir_temp->args1_type == DATA_UFLOAT8 || ir_temp->args1_type == DATA_UFLOAT16 ||
                                    ir_temp->args1_type == DATA_UFLOAT32 || ir_temp->args1_type == DATA_UFLOAT128) ||
                                   (ir_temp->args2_type == DATA_FLOAT || ir_temp->args2_type == DATA_UFLOAT ||
                                    ir_temp->args2_type == DATA_FLOAT64 || ir_temp->args2_type == DATA_UFLOAT64 ||
                                    ir_temp->args2_type == DATA_FLOAT8 || ir_temp->args2_type == DATA_FLOAT16 ||
                                    ir_temp->args2_type == DATA_FLOAT32 || ir_temp->args2_type == DATA_FLOAT128 ||
                                    ir_temp->args2_type == DATA_UFLOAT8 || ir_temp->args2_type == DATA_UFLOAT16 ||
                                    ir_temp->args2_type == DATA_UFLOAT32 || ir_temp->args2_type == DATA_UFLOAT128);

                if (is_float_op) {
                    cache_flush_all(output);
                    cg_load_float(output, ir_temp->args1, ir_temp->args1_type);
                    cg_load_float(output, ir_temp->args2, ir_temp->args2_type);

                    bool float_result_is_int = true;
                    switch (ir_temp->op) {
                        case IR_ADD:
                            fprintf(output, "  faddp\n");
                            float_result_is_int = false;
                            break;
                        case IR_SUB:
                            fprintf(output, "  fsubp\n");
                            float_result_is_int = false;
                            break;
                        case IR_MUL:
                            fprintf(output, "  fmulp\n");
                            float_result_is_int = false;
                            break;
                        case IR_DIV:
                        case IR_UDIV:
                            fprintf(output, "  fdivp\n");
                            float_result_is_int = false;
                            break;
                        case IR_MOD:
                        case IR_UMOD:
                            has_float_lib = true;
                            fprintf(output, "  call _fmod_fpu\n");
                            float_result_is_int = false;
                            break;
                        case IR_JE:
                            has_float_lib = true;
                            fprintf(output, "  sub esp, 8\n");
                            fprintf(output, "  fstp dword [esp+4]\n");
                            fprintf(output, "  fstp dword [esp]\n");
                            fprintf(output, "  call _feq\n");
                            fprintf(output, "  add esp, 8\n");
                            break;
                        case IR_JNE:
                            has_float_lib = true;
                            fprintf(output, "  sub esp, 8\n");
                            fprintf(output, "  fstp dword [esp+4]\n");
                            fprintf(output, "  fstp dword [esp]\n");
                            fprintf(output, "  call _fne\n");
                            fprintf(output, "  add esp, 8\n");
                            break;
                        case IR_JG:
                            has_float_lib = true;
                            fprintf(output, "  sub esp, 8\n");
                            fprintf(output, "  fstp dword [esp+4]\n");
                            fprintf(output, "  fstp dword [esp]\n");
                            fprintf(output, "  call _fgt\n");
                            fprintf(output, "  add esp, 8\n");
                            break;
                        case IR_JGE:
                            has_float_lib = true;
                            fprintf(output, "  sub esp, 8\n");
                            fprintf(output, "  fstp dword [esp+4]\n");
                            fprintf(output, "  fstp dword [esp]\n");
                            fprintf(output, "  call _fge\n");
                            fprintf(output, "  add esp, 8\n");
                            break;
                        case IR_JL:
                            has_float_lib = true;
                            fprintf(output, "  sub esp, 8\n");
                            fprintf(output, "  fstp dword [esp+4]\n");
                            fprintf(output, "  fstp dword [esp]\n");
                            fprintf(output, "  call _flt\n");
                            fprintf(output, "  add esp, 8\n");
                            break;
                        case IR_JLE:
                            has_float_lib = true;
                            fprintf(output, "  sub esp, 8\n");
                            fprintf(output, "  fstp dword [esp+4]\n");
                            fprintf(output, "  fstp dword [esp]\n");
                            fprintf(output, "  call _fle\n");
                            fprintf(output, "  add esp, 8\n");
                            break;
                        default:
                            break;
                    }

                    if (ir_temp->op == IR_SUB &&
                        (res_type == DATA_UFLOAT || res_type == DATA_UFLOAT8 ||
                         res_type == DATA_UFLOAT16 || res_type == DATA_UFLOAT32 ||
                         res_type == DATA_UFLOAT64 || res_type == DATA_UFLOAT128)) {
                        fprintf(output, "  ftst\n");
                        fprintf(output, "  fstsw ax\n");
                        fprintf(output, "  sahf\n");
                        fprintf(output, "  jae .L_ufclamp_%d\n", uf_clamp_label);
                        fprintf(output, "  fstp st0\n");
                        fprintf(output, "  fldz\n");
                        fprintf(output, ".L_ufclamp_%d:\n", uf_clamp_label);
                        uf_clamp_label++;
                         }
                    cg_store(output, ir_temp->result, res_type, !float_result_is_int);
                }
                else {
                    if (ir_temp->args2_type == DATA_STRING &&
                        ir_temp->args1_type != DATA_STRING) {
                        cg_load(output, ir_temp->args2, ir_temp->args2_type);
                        fprintf(output, "  push eax\n");

                        cg_load(output, ir_temp->args1, ir_temp->args1_type);
                    } else {
                        DataType right_loaded = cg_load(output, ir_temp->args2, ir_temp->args2_type);
                        fprintf(output, "  push eax\n");
                        cg_load(output, ir_temp->args1, ir_temp->args1_type);
                    }

                    bool is_str_cmp =
                        (ir_temp->op == IR_JE || ir_temp->op == IR_JNE) &&
                        (ir_temp->args1_type == DATA_STRING ||
                         ir_temp->args2_type == DATA_STRING);

                    if (is_str_cmp) {
                        fprintf(output, "  push eax\n");
                        fprintf(output, "  push dword [esp+4]\n");
                        fprintf(output, "  call _str_equal\n");
                        fprintf(output, "  add esp, 8\n");
                        if (ir_temp->op == IR_JNE) {
                            fprintf(output, "  test eax, eax\n");
                            fprintf(output, "  sete al\n");
                            fprintf(output, "  movzx eax, al\n");
                        }
                        fprintf(output, "  add esp, 4\n");
                        cg_store(output, ir_temp->result, res_type, false);
                        has_string_lib = true;
                        has_memory_lib = true;
                        has_gc = true;
                        break;
                    }

                    if (ir_temp->op == IR_SHL || ir_temp->op == IR_SHR ||
                        ir_temp->op == IR_SAR)
                        cache_flush_reg(output, R_ECX);
                    if (ir_temp->op == IR_DIV || ir_temp->op == IR_MOD ||
                        ir_temp->op == IR_UDIV || ir_temp->op == IR_UMOD)
                        cache_flush_reg(output, R_EDX);
                    switch (ir_temp->op) {
                        case IR_ADD:
                            fprintf(output, "  add eax, dword [esp]\n");
                            break;
                        case IR_SUB:
                            fprintf(output, "  sub eax, dword [esp]\n");
                            break;
                        case IR_MUL:
                            fprintf(output, "  imul eax, dword [esp]\n");
                            break;
                        case IR_DIV:
                            fprintf(output, "  push edx\n");
                            fprintf(output, "  cdq\n");
                            fprintf(output, "  idiv dword [esp+4]\n");
                            fprintf(output, "  pop edx\n");
                            break;
                        case IR_MOD:
                            fprintf(output, "  push edx\n");
                            fprintf(output, "  cdq\n");
                            fprintf(output, "  idiv dword [esp+4]\n");
                            fprintf(output, "  mov eax, edx\n");
                            fprintf(output, "  pop edx\n");
                            break;
                        case IR_UDIV:
                            fprintf(output, "  push edx\n");
                            fprintf(output, "  xor edx, edx\n");
                            fprintf(output, "  div dword [esp+4]\n");
                            fprintf(output, "  pop edx\n");
                            break;
                        case IR_UMOD:
                            fprintf(output, "  push edx\n");
                            fprintf(output, "  xor edx, edx\n");
                            fprintf(output, "  div dword [esp+4]\n");
                            fprintf(output, "  mov eax, edx\n");
                            fprintf(output, "  pop edx\n");
                            break;
                        case IR_JE:
                            fprintf(output, "  cmp eax, dword [esp]\n");
                            fprintf(output, "  sete al\n");
                            fprintf(output, "  movzx eax, al\n");
                            break;
                        case IR_JNE:
                            fprintf(output, "  cmp eax, dword [esp]\n");
                            fprintf(output, "  setne al\n");
                            fprintf(output, "  movzx eax, al\n");
                            break;
                        case IR_JG:
                            fprintf(output, "  cmp eax, dword [esp]\n");
                            fprintf(output, "  setg al\n");
                            fprintf(output, "  movzx eax, al\n");
                            break;
                        case IR_JL:
                            fprintf(output, "  cmp eax, dword [esp]\n");
                            fprintf(output, "  setl al\n");
                            fprintf(output, "  movzx eax, al\n");
                            break;
                        case IR_JGE:
                            fprintf(output, "  cmp eax, dword [esp]\n");
                            fprintf(output, "  setge al\n");
                            fprintf(output, "  movzx eax, al\n");
                            break;
                        case IR_JLE:
                            fprintf(output, "  cmp eax, dword [esp]\n");
                            fprintf(output, "  setle al\n");
                            fprintf(output, "  movzx eax, al\n");
                            break;
                        case IR_ULT:
                            fprintf(output, "  cmp eax, dword [esp]\n");
                            fprintf(output, "  setb al\n");
                            fprintf(output, "  movzx eax, al\n");
                            break;
                        case IR_ULE:
                            fprintf(output, "  cmp eax, dword [esp]\n");
                            fprintf(output, "  setbe al\n");
                            fprintf(output, "  movzx eax, al\n");
                            break;
                        case IR_UGT:
                            fprintf(output, "  cmp eax, dword [esp]\n");
                            fprintf(output, "  seta al\n");
                            fprintf(output, "  movzx eax, al\n");
                            break;
                        case IR_UGE:
                            fprintf(output, "  cmp eax, dword [esp]\n");
                            fprintf(output, "  setae al\n");
                            fprintf(output, "  movzx eax, al\n");
                            break;
                        case IR_BITWISE_AND:
                            fprintf(output, "  and eax, dword [esp]\n");
                            break;
                        case IR_BITWISE_OR:
                            fprintf(output, "  or eax, dword [esp]\n");
                            break;
                        case IR_SHL:
                            fprintf(output, "  mov ecx, dword [esp]\n");
                            fprintf(output, "  shl eax, cl\n");
                            break;
                        case IR_SHR:
                            fprintf(output, "  mov ecx, dword [esp]\n");
                            fprintf(output, "  shr eax, cl\n");
                            break;
                        case IR_SAR:
                            fprintf(output, "  mov ecx, dword [esp]\n");
                            fprintf(output, "  sar eax, cl\n");
                            break;
                        case IR_BITWISE_XOR:
                            fprintf(output, "  xor eax, dword [esp]\n");
                            break;
                        default:
                            break;
                    }
                    fprintf(output, "  add esp, 4\n");
                    cg_store(output, ir_temp->result, res_type, false);
                }
                break;
            }

            case IR_STR_FROM_STRING: {
                int result_offset = temp_map_get_offset(ir_temp->result);

                cg_load(output, ir_temp->args1, DATA_STRING);

                fprintf(output, "  mov dword [ebp-%d], eax\n", result_offset);

                if (ir_temp->args2 && ir_temp->args2[0] != '\0') {
                    fprintf(output, "  mov dword [ebp-%d], %s\n",
                            result_offset - 4, ir_temp->args2);
                } else {
                    fprintf(output, "  mov eax, dword [eax - 4]\n");
                    fprintf(output, "  mov dword [ebp-%d], eax\n",
                            result_offset - 4);
                }

                if (ir_temp->result && is_temp(ir_temp->result)) {
                    temp_map_add(ir_temp->result, DATA_STR);
                }
                break;
            }

            case IR_STR_LENGTH: {
                cache_flush_all(output);
                int str_off = temp_map_get_offset(ir_temp->args1);
                fprintf(output, "  mov eax, dword [ebp-%d]\n", str_off);
                cg_store(output, ir_temp->result, DATA_INT, false);
                break;
            }

            case IR_STR_INDEX: {
                cache_flush_all(output);
                int str_off = temp_map_get_offset(ir_temp->args1);
                cg_load(output, ir_temp->args2, DATA_INT);
                fprintf(output, "  mov ecx, dword [ebp-%d]\n", str_off);
                fprintf(output, "  movzx eax, byte [ecx + eax]\n");
                cg_store(output, ir_temp->result, DATA_CHAR, false);
                break;
            }

            case IR_DEREF: {
                cache_flush_all(output);
                char* ptr_name = ir_temp->args1;

                cg_load(output, ptr_name, DATA_INT);

                DataType dt = ir_temp->data_type;
                if (dt == DATA_FLOAT || dt == DATA_UFLOAT) {
                    fprintf(output, "  fld dword [eax]\n");
                    cg_store(output, ir_temp->result, dt, true);
                } else if (dt == DATA_CHAR || dt == DATA_BOOL) {
                    fprintf(output, "  movzx eax, byte [eax]\n");
                    cg_store(output, ir_temp->result, dt, false);
                } else if (dt == DATA_INT64 || dt == DATA_UINT64) {
                    cache_flush_reg(output, R_EDX);
                    fprintf(output, "  mov ecx, eax\n");
                    fprintf(output, "  mov eax, dword [ecx]\n");
                    fprintf(output, "  mov edx, dword [ecx+4]\n");
                    cg_store(output, ir_temp->result, dt, false);
                } else if (dt == DATA_FLOAT64 || dt == DATA_UFLOAT64) {
                    fprintf(output, "  fld qword [eax]\n");
                    cg_store(output, ir_temp->result, dt, true);
                } else {
                    fprintf(output, "  mov eax, dword [eax]\n");
                    cg_store(output, ir_temp->result, DATA_INT, false);
                }

                if (ir_temp->result && is_temp(ir_temp->result)) {
                    temp_map_add(ir_temp->result, ir_temp->data_type);
                }
                break;
            }

            case IR_ADDRESS_OF: {
                cache_flush_all(output);
                char* var_name = ir_temp->args1;

                if (ir_temp->is_function_addr) {
                    fprintf(output, "  mov eax, %s\n", var_name);
                }
                else if (is_temp(var_name)) {
                    int tidx = get_const(var_name);
                    if (tidx != -1 && consts->storage[tidx] == STORAGE_LOCAL) {
                        fprintf(output, "  lea eax, [ebp-%d]\n",
                                -consts->offset[tidx]);
                    } else {
                        int off = temp_map_get_offset(var_name);
                        fprintf(output, "  lea eax, [ebp-%d]\n", off);
                    }
                }
                else {
                    int idx = get_const(var_name);
                    if (idx != -1) {
                        char addr[100];
                        switch (consts->storage[idx]) {
                            case STORAGE_GLOBAL:
                                snprintf(addr, sizeof(addr), "%s", var_name);
                                break;
                            case STORAGE_PARAM:
                                snprintf(addr, sizeof(addr), "ebp+%d",
                                         consts->offset[idx]);
                                break;
                            case STORAGE_LOCAL:
                                snprintf(addr, sizeof(addr), "ebp-%d",
                                         -consts->offset[idx]);
                                break;
                            case STORAGE_STATIC_LOCAL:
                            case STORAGE_STATIC_GUARD:
                                snprintf(addr, sizeof(addr), "%s",
                                         consts->name[idx] ? consts->name[idx]
                                                           : var_name);
                                break;
                            default:
                                snprintf(addr, sizeof(addr), "%s", var_name);
                                break;
                        }
                        fprintf(output, "  lea eax, [%s]\n", addr);
                    } else {
                        FATAL(ERR_CG_UNKNOWN_VAR, LOC_NONE,
                              "unknown variable '%s' for address-of", var_name);
                    }
                }
                cg_store(output, ir_temp->result, DATA_INT, false);
                if (!ir_temp->is_function_addr) {
                    int addr_idx = get_const(var_name);
                    if (addr_idx != -1 && consts->is_array[addr_idx]
                        && !consts->is_dynamic_array[addr_idx]) {
                        temp_map_set_array_size(ir_temp->result,
                                                consts->array_size[addr_idx]);
                    }
                }
                break;
            }

            case IR_PTR_STORE: {
                cache_flush_all(output);
                char* ptr_name = ir_temp->args1;
                char* val_name = ir_temp->args2;
                DataType dt = ir_temp->data_type;

                cg_load(output, ptr_name, DATA_INT);
                fprintf(output, "  test eax, eax\n");
                fprintf(output, "  jz _null_ptr_error\n");
                has_bounds_check = true;
                fprintf(output, "  push eax\n");

                DataType val_type = cg_load(output, val_name, dt);

                cache_flush_all(output);
                fprintf(output, "  pop ecx\n");

                if (dt == DATA_FLOAT || dt == DATA_UFLOAT) {
                    fprintf(output, "  fstp dword [ecx]\n");
                } else if (dt == DATA_CHAR || dt == DATA_BOOL) {
                    fprintf(output, "  mov [ecx], al\n");
                    fprintf(output, "  mov byte [ecx+1], 0\n");
                    fprintf(output, "  mov byte [ecx+2], 0\n");
                    fprintf(output, "  mov byte [ecx+3], 0\n");
                } else if (dt == DATA_INT64 || dt == DATA_UINT64
                           || dt == DATA_FLOAT64 || dt == DATA_UFLOAT64
                           || dt == DATA_STR) {
                    fprintf(output, "  mov [ecx], eax\n");
                    fprintf(output, "  mov [ecx+4], edx\n");
                } else {
                    fprintf(output, "  mov [ecx], eax\n");
                }
                break;
            }

            case IR_BITWISE_NOT: {
                char* operand = ir_temp->args1;
                int result_offset = temp_map_get_offset(ir_temp->result);

                if (is_temp(operand)) {
                    cg_store(output, ir_temp->result, ir_temp->data_type, false);
                } else if (get_const(operand) != -1) {
                    load_var(output, operand);
                } else {
                    fprintf(output, "  mov eax, %s\n", operand);
                }

                fprintf(output, "  not eax\n");
                fprintf(output, "  mov dword [ebp-%d], eax\n", result_offset);
                break;
            }

            case IR_PRINT: {
                cache_flush_all(output);
                const char* print_fn = ir_temp->is_err ? "_print_err" : "_print";
                if (ir_temp->is_err) has_print = true;
                char* print_operand = ir_temp->args1;
                if (!ir_temp->call_func_args_type || ir_temp->call_func_args_num < 1) {
                    fprintf(output, "  ; WARNING: malformed IR_PRINT, skipping\n");
                    break;
                }
                DataType print_type = ir_temp->call_func_args_type[0];

                if (print_type == DATA_INT64 || print_type == DATA_UINT64) {
                    cg_load(output, print_operand, print_type);
                    fprintf(output, "  push edx\n");
                    fprintf(output, "  push eax\n");
                    fprintf(output, "  call _int64_to_str\n");
                    fprintf(output, "  add esp, 8\n");
                    fprintf(output, "  push %d\n", map_to_runtime_type(DATA_STRING));
                    fprintf(output, "  push eax\n");
                    fprintf(output, "  call %s\n", print_fn);
                    fprintf(output, "  add esp, 8\n");
                    cache_flush_all(output);
                    has_64bit_ops = true;
                    has_print = true;
                    has_string_lib = true;
                    has_memory_lib = true;
                    has_gc = true;
                    break;
                }

                if (print_type == DATA_STR) {
                    if (is_temp(print_operand)) {
                        int off = temp_map_get_offset(print_operand);
                        fprintf(output, "  mov esi, dword [ebp-%d]\n", off);
                        fprintf(output, "  mov ecx, dword [ebp-%d]\n", off - 4);
                    } else {
                        char addr_buf[128] = {0};
                        if (!cg_address(output, print_operand, addr_buf, sizeof(addr_buf))) {
                            FATAL(ERR_CG_UNKNOWN_VAR, LOC_NONE,
                                  "print str: cannot get address of '%s'", print_operand);
                        }
                        fprintf(output, "  lea eax, %s\n", addr_buf);
                        fprintf(output, "  mov esi, [eax]\n");
                        fprintf(output, "  mov ecx, [eax+4]\n");
                    }

                    fprintf(output, "  push ebx\n");
                    fprintf(output, "  mov ebx, ecx\n");
                    fprintf(output, "  add ecx, 5\n");
                    fprintf(output, "  and ecx, -4\n");
                    fprintf(output, "  mov edx, ecx\n");
                    fprintf(output, "  push edx\n");
                    fprintf(output, "  sub esp, ecx\n");
                    fprintf(output, "  mov edi, esp\n");
                    fprintf(output, "  mov eax, ebx\n");
                    fprintf(output, "  mov [edi], eax\n");
                    fprintf(output, "  add edi, 4\n");
                    fprintf(output, "  mov ecx, ebx\n");
                    fprintf(output, "  cld\n");
                    fprintf(output, "  rep movsb\n");
                    fprintf(output, "  mov byte [edi], 0\n");
                    fprintf(output, "  pop ebx\n");
                    fprintf(output, "  push edx\n");
                    fprintf(output, "  push %d\n", map_to_runtime_type(DATA_STRING));
                    fprintf(output, "  lea eax, [esp+8]\n");
                    fprintf(output, "  push eax\n");
                    fprintf(output, "  call %s\n", print_fn);
                    fprintf(output, "  add esp, 8\n");
                    cache_flush_all(output);
                    fprintf(output, "  pop edx\n");
                    fprintf(output, "  add esp, edx\n");
                    has_print = true;
                    break;
                }

                fprintf(output, "  push %d\n", map_to_runtime_type(print_type));

                if (is_temp(print_operand) || get_const(print_operand) != -1) {
                    cg_load(output, print_operand, print_type);
                    if (print_type == DATA_FLOAT || print_type == DATA_UFLOAT ||
                        print_type == DATA_FLOAT64 || print_type == DATA_UFLOAT64 ||
                        print_type == DATA_FLOAT8 || print_type == DATA_FLOAT16 ||
                        print_type == DATA_FLOAT32 || print_type == DATA_FLOAT128 ||
                        print_type == DATA_UFLOAT8 || print_type == DATA_UFLOAT16 ||
                        print_type == DATA_UFLOAT32 || print_type == DATA_UFLOAT128) {
                        fprintf(output, "  sub esp, 4\n");
                        fprintf(output, "  fstp dword [esp]\n");
                        } else {
                            fprintf(output, "  push eax\n");
                        }
                }
                else if (print_type == DATA_STRING) {
                    char* str_label = add_const(DATA_STRING, print_operand, 0, DATA_STRING);
                    fprintf(output, "  push %s\n", str_label);
                }
                else if (print_type == DATA_FLOAT || print_type == DATA_UFLOAT ||
                         print_type == DATA_FLOAT64 || print_type == DATA_UFLOAT64 ||
                         print_type == DATA_FLOAT8 || print_type == DATA_FLOAT16 ||
                         print_type == DATA_FLOAT32 || print_type == DATA_FLOAT128 ||
                         print_type == DATA_UFLOAT8 || print_type == DATA_UFLOAT16 ||
                         print_type == DATA_UFLOAT32 || print_type == DATA_UFLOAT128) {
                    char* label = add_const(DATA_FLOAT, print_operand, 0, DATA_FLOAT);
                    fprintf(output, "  push dword [%s]\n", label);
                         }
                else if (is_number(print_operand)
                         && strchr(print_operand, '.') != NULL) {
                    char* label = add_const(DATA_FLOAT, print_operand, 0, DATA_FLOAT);
                    if (print_type == DATA_FLOAT || print_type == DATA_UFLOAT ||
                        print_type == DATA_FLOAT32 || print_type == DATA_UFLOAT32 ||
                        print_type == DATA_FLOAT64 || print_type == DATA_UFLOAT64) {
                        fprintf(output, "  push dword [%s]\n", label);
                    } else {
                        fprintf(output, "  fld dword [%s]\n", label);
                        fprintf(output, "  sub esp, 4\n");
                        fprintf(output, "  fisttp dword [esp]\n");
                        fprintf(output, "  mov eax, dword [esp]\n");
                        fprintf(output, "  add esp, 4\n");
                        fprintf(output, "  push eax\n");
                    }
                }
                else {
                    fprintf(output, "  push %s\n", print_operand);
                }

                fprintf(output, "  call %s\n", print_fn);
                fprintf(output, "  add esp, 8\n");
                cache_flush_all(output);
                has_print = true;
                break;
            }

            case IR_INPUT: {
                cache_flush_all(output);
                has_input = true;
                has_memory_lib = true;
                int num = ir_temp->call_func_args_num;
                char* tpl = ir_temp->args1;
                int tpl_off = temp_map_get_offset(tpl);

                fprintf(output, "  sub esp, %d\n", num * 8);
                for (int i = 0; i < num; i++) {
                    DataType t = ir_temp->call_func_args_type[i];
                    int code = (t == DATA_FLOAT || t == DATA_UFLOAT) ? 1 :
                               (t == DATA_CHAR) ? 2 : (t == DATA_STRING) ? 3 :
                               (t == DATA_BOOL) ? 4 : 0;
                    fprintf(output, "  mov dword [esp+%d], %d\n", i*4, code);

                    int idx = get_const(ir_temp->label[i]);
                    if (idx != -1) {
                        char addr[100];
                        switch (consts->storage[idx]) {
                            case STORAGE_GLOBAL: sprintf(addr, "%s", ir_temp->label[i]); break;
                            case STORAGE_PARAM:  sprintf(addr, "ebp+%d", consts->offset[idx]); break;
                            case STORAGE_LOCAL:  sprintf(addr, "ebp-%d", -consts->offset[idx]); break;
                        }
                        fprintf(output, "  lea eax, [%s]\n", addr);
                        fprintf(output, "  mov [esp+%d], eax\n", num*4 + i*4);
                    }
                }
                fprintf(output, "  mov eax, esp\n");
                fprintf(output, "  lea ecx, [esp+%d]\n", num*4);
                fprintf(output, "  push ecx\n");
                fprintf(output, "  push eax\n");
                fprintf(output, "  push %d\n", num);
                fprintf(output, "  push dword [ebp-%d]\n", tpl_off);
                fprintf(output, "  call _input_parse\n");
                fprintf(output, "  add esp, 16\n");
                fprintf(output, "  add esp, %d\n", num * 8);
                cache_flush_all(output);
                break;
            }

            case IR_VAR: {
                char* var_name = ir_temp->result;
                DataType var_type = ir_temp->data_type;
                char* src = ir_temp->args1;
                DataType src_type = ir_temp->args1_type;
                int dest_idx = get_const(var_name);
                int var_stack_sz = (ir_temp->stack_bytes > 0) ? ir_temp->stack_bytes : 4;
                var_stack_sz = (var_stack_sz + 3) & ~3;
                if (dest_idx == -1) {
                    if (ir_temp->is_static_global) {
                        if (var_type == DATA_STRING && src != NULL
                            && !is_temp(src) && get_const(src) == -1) {
                            char* str_label = add_const(DATA_STRING, src, 0, DATA_STRING);
                            add_symbol(DATA_STRING, str_label, var_name, TYPE_VAR,
                                       DATA_STRING, STORAGE_GLOBAL, 0, false, 0, false);
                        } else {
                            add_symbol(var_type, src, var_name, TYPE_VAR, src_type,
                                       STORAGE_GLOBAL, 0, (var_stack_sz > 4), var_stack_sz,
                                       ir_temp->is_dynamic_array);
                        }
                        break;
                    }
                    if (in_function) {
                        bool is_static = ir_temp->is_static_local;
                        current_local_offset -= var_stack_sz;
                        add_symbol(var_type, src, var_name, TYPE_VAR, src_type,
                                   is_static ? STORAGE_STATIC_LOCAL : STORAGE_LOCAL,
                                   is_static ? 0 : current_local_offset,
                                   (var_stack_sz > 4),
                                   var_stack_sz,ir_temp->is_dynamic_array);
                    } else {
                        if (var_type == DATA_STRING && src != NULL
                            && !is_temp(src) && get_const(src) == -1) {
                            char* str_label = add_const(DATA_STRING, src, 0, DATA_STRING);
                            add_symbol(DATA_STRING, str_label, var_name, TYPE_VAR,
                                       DATA_STRING, STORAGE_GLOBAL, 0, false, 0, false);
                            } else {
                                add_symbol(var_type, src, var_name, TYPE_VAR, src_type,
                                           STORAGE_GLOBAL, 0, (var_stack_sz > 4), var_stack_sz,
                                           ir_temp->is_dynamic_array);
                            }
                        break;
                    }
                }
                dest_idx = get_const(var_name);
                if (dest_idx == -1) {
                    FATAL(ERR_CG_UNKNOWN_VAR, LOC_NONE,
                          "variable '%s' not in symbol table", var_name);
                }
                if (consts->storage[dest_idx] == STORAGE_GLOBAL ||
                    consts->storage[dest_idx] == STORAGE_STATIC_LOCAL) {
                    if (consts->storage[dest_idx] == STORAGE_GLOBAL
                        && ir_temp->args1 != NULL
                        && (consts->value[dest_idx] == NULL
                            || strcmp(consts->value[dest_idx], "0") == 0)) {
                        consts->value[dest_idx] = strdup(ir_temp->args1);
                        if (ir_temp->args1_type != DATA_ERROR) {
                            consts->value_type[dest_idx] = ir_temp->args1_type;
                        }
                    }
                    break;
                }

                if (src != NULL) {
                    if (strcmp(src, "false") == 0 || strcmp(src, "true") == 0) {
                        int val = (strcmp(src, "true") == 0) ? 1 : 0;
                        fprintf(output, "  mov eax, %d\n", val);
                        cg_store(output, var_name, var_type, false);
                        break;
                    }

                    if (var_type == DATA_STRING && !is_temp(src) &&
                        get_const(src) == -1) {
                        char* const_label = add_const(DATA_STRING, src, 0, DATA_STRING);
                        fprintf(output, "  mov eax, %s\n", const_label);
                        cg_store(output, var_name, DATA_STRING, false);
                        break;
                        }

                    if ((var_type == DATA_FLOAT || var_type == DATA_UFLOAT) &&
                        !is_temp(src) && get_const(src) == -1) {
                        if (is_number(src) || src[0] == '.' ||
                            (src[0] >= '0' && src[0] <= '9') || src[0] == '-') {
                            char* const_label = add_const(DATA_FLOAT, src, 0, DATA_FLOAT);
                            fprintf(output, "  fld dword [%s]\n", const_label);
                            cg_store(output, var_name, var_type, true);
                            break;
                            }
                        }

                    if (var_type == DATA_CHAR && !is_temp(src) &&
                        get_const(src) == -1 && !is_number(src)) {
                        char ch = src[0];
                        if (src[0] == '\'') ch = src[1];
                        fprintf(output, "  mov eax, %d\n", ch);
                        cg_store(output, var_name, DATA_CHAR, false);
                        break;
                        }
                }

                if (src != NULL && src[0] != '\0' && !is_temp(src) && !is_number(src)
                    && strcmp(src, "false") != 0 && strcmp(src, "true") != 0
                    && get_const(src) == -1) {
                    fprintf(output, "  mov eax, %s\n", src);
                    cg_store(output, var_name, var_type, false);
                    break;
                    }

                DataType loaded_type = cg_load(output, src, src_type);

                if ((var_type == DATA_FLOAT || var_type == DATA_UFLOAT)
                    && !(loaded_type == DATA_FLOAT || loaded_type == DATA_UFLOAT || loaded_type == DATA_FLOAT32 || loaded_type == DATA_UFLOAT32)) {
                    if (is_temp((char*)src)) {
                        int src_off_sav = temp_map_get_offset(src);;
                        fprintf(output, "  fld dword [ebp-%d]\n", src_off_sav);
                        loaded_type = var_type;
                    } else if (var_type == DATA_UFLOAT) {
                        fprintf(output, "  push 0\n");
                        fprintf(output, "  push eax\n");
                        fprintf(output, "  fild qword [esp]\n");
                        fprintf(output, "  add esp, 8\n");
                        loaded_type = var_type;
                    } else {
                        fprintf(output, "  push eax\n");
                        fprintf(output, "  fild dword [esp]\n");
                        fprintf(output, "  add esp, 4\n");
                        loaded_type = var_type;
                    }
                }

                if ((var_type == DATA_INT || var_type == DATA_UINT ||
                    var_type == DATA_CHAR || var_type == DATA_BOOL)
                    && (loaded_type == DATA_FLOAT || loaded_type == DATA_UFLOAT)) {
                    fprintf(output, "  sub esp, 4\n");
                    fprintf(output, "  fisttp dword [esp]\n");
                    fprintf(output, "  mov eax, dword [esp]\n");
                    fprintf(output, "  add esp, 4\n");
                    loaded_type = var_type;
                }

                if ((var_type == DATA_INT64 || var_type == DATA_UINT64)
                    && (loaded_type == DATA_INT || loaded_type == DATA_INT8
                        || loaded_type == DATA_INT16 || loaded_type == DATA_INT32
                        || loaded_type == DATA_UINT || loaded_type == DATA_UINT8
                        || loaded_type == DATA_UINT16 || loaded_type == DATA_UINT32
                        || loaded_type == DATA_CHAR || loaded_type == DATA_BOOL)) {
                    bool src_unsigned = (loaded_type == DATA_UINT
                                      || loaded_type == DATA_UINT8
                                      || loaded_type == DATA_UINT16
                                      || loaded_type == DATA_UINT32
                                      || loaded_type == DATA_CHAR
                                      || loaded_type == DATA_BOOL);
                    if (src_unsigned) {
                        fprintf(output, "  xor edx, edx\n");
                    } else {
                        fprintf(output, "  cdq\n");
                    }
                }

                cg_store(output, var_name, var_type,
                         (loaded_type == DATA_FLOAT || loaded_type == DATA_UFLOAT ||
                          loaded_type == DATA_FLOAT64 || loaded_type == DATA_UFLOAT64 ||
                          loaded_type == DATA_FLOAT8 || loaded_type == DATA_FLOAT16 ||
                          loaded_type == DATA_FLOAT32 || loaded_type == DATA_FLOAT128 ||
                          loaded_type == DATA_UFLOAT8 || loaded_type == DATA_UFLOAT16 ||
                          loaded_type == DATA_UFLOAT32 || loaded_type == DATA_UFLOAT128));
                break;
            }

            case IR_VAR_ASS: {
                char* dest = ir_temp->result;
                char* src = ir_temp->args1;
                DataType src_type = ir_temp->args1_type;

                int dest_idx = get_const(dest);
                DataType dest_type;
                if (dest_idx == -1 && is_temp(dest)) {
                    DataType t = temp_map_get(dest);
                    if (t == DATA_ERROR) t = ir_temp->data_type;
                    dest_type = t;
                } else if (dest_idx == -1) {
                    if (ir_temp->is_static_local) {
                        add_symbol(ir_temp->data_type, "", dest, TYPE_VAR, ir_temp->data_type,
                                   STORAGE_STATIC_LOCAL, 0, false, 0,
                                   ir_temp->is_dynamic_array);
                    } else if (in_function) {
                        current_local_offset -= 4;
                        add_symbol(ir_temp->data_type, "", dest, TYPE_VAR, ir_temp->data_type,
                                   STORAGE_LOCAL, current_local_offset, false, 0,
                                   ir_temp->is_dynamic_array);
                    } else {
                        add_symbol(ir_temp->data_type, "", dest, TYPE_VAR, ir_temp->data_type,
                                   STORAGE_GLOBAL, 0, false, 0,
                                   ir_temp->is_dynamic_array);
                    }
                    dest_idx = get_const(dest);
                    if (dest_idx == -1) {
                        FATAL(ERR_CG_UNKNOWN_VAR, LOC_NONE,
                              "variable '%s' not in symbol table after lazy creation", dest);
                    }
                    dest_type = consts->type[dest_idx];
                } else {
                    dest_type = consts->type[dest_idx];
                }

                if (src != NULL) {
                    if (strcmp(src, "false") == 0 || strcmp(src, "true") == 0) {
                        int val = (strcmp(src, "true") == 0) ? 1 : 0;
                        fprintf(output, "  mov eax, %d\n", val);
                        cg_store(output, dest, dest_type, false);
                        break;
                    }
                    if (dest_type == DATA_STRING && !is_temp(src) &&
                        get_const(src) == -1) {
                        char* const_label = add_const(DATA_STRING, src, 0, DATA_STRING);
                        fprintf(output, "  mov eax, %s\n", const_label);
                        cg_store(output, dest, DATA_STRING, false);
                        break;
                        }
                    if ((dest_type == DATA_FLOAT || dest_type == DATA_UFLOAT) &&
                        !is_temp(src) && get_const(src) == -1) {
                        if (is_number(src) || src[0] == '.' ||
                            (src[0] >= '0' && src[0] <= '9') || src[0] == '-') {
                            char* const_label = add_const(DATA_FLOAT, src, 0, DATA_FLOAT);
                            fprintf(output, "  fld dword [%s]\n", const_label);
                            cg_store(output, dest, dest_type, true);
                            break;
                            }
                        }
                    if (dest_type == DATA_CHAR && !is_temp(src) &&
                        get_const(src) == -1 && !is_number(src)) {
                        char ch = src[0];
                        if (src[0] == '\'') ch = src[1];
                        fprintf(output, "  mov eax, %d\n", ch);
                        cg_store(output, dest, DATA_CHAR, false);
                        break;
                        }
                }

                if (src != NULL && src[0] != '\0' && !is_temp(src) && !is_number(src)
                    && strcmp(src, "false") != 0 && strcmp(src, "true") != 0
                    && get_const(src) == -1) {
                    fprintf(output, "  mov eax, %s\n", src);
                    cg_store(output, dest, dest_type, false);
                    break;
                    }

                DataType loaded_type = cg_load(output, src, src_type);

                if ((dest_type == DATA_FLOAT || dest_type == DATA_UFLOAT)
                    && !(loaded_type == DATA_FLOAT || loaded_type == DATA_UFLOAT || loaded_type == DATA_FLOAT32 || loaded_type == DATA_UFLOAT32)) {
                    if (is_temp((char*)src)) {
                        int src_off_dest = temp_map_get_offset(src);
                        fprintf(output, "  fld dword [ebp-%d]\n", src_off_dest);
                        loaded_type = dest_type;
                    } else if (dest_type == DATA_UFLOAT) {
                        fprintf(output, "  push 0\n");
                        fprintf(output, "  push eax\n");
                        fprintf(output, "  fild qword [esp]\n");
                        fprintf(output, "  add esp, 8\n");
                        loaded_type = dest_type;
                    } else {
                        fprintf(output, "  push eax\n");
                        fprintf(output, "  fild dword [esp]\n");
                        fprintf(output, "  add esp, 4\n");
                        loaded_type = dest_type;
                    }
                }

                if ((dest_type == DATA_INT || dest_type == DATA_UINT ||
                    dest_type == DATA_CHAR || dest_type == DATA_BOOL)
                    && (loaded_type == DATA_FLOAT || loaded_type == DATA_UFLOAT)) {
                    fprintf(output, "  sub esp, 4\n");
                    fprintf(output, "  fisttp dword [esp]\n");
                    fprintf(output, "  mov eax, dword [esp]\n");
                    fprintf(output, "  add esp, 4\n");
                    loaded_type = dest_type;
                }

                if ((dest_type == DATA_INT64 || dest_type == DATA_UINT64)
                    && (loaded_type == DATA_INT || loaded_type == DATA_INT8
                        || loaded_type == DATA_INT16 || loaded_type == DATA_INT32
                        || loaded_type == DATA_UINT || loaded_type == DATA_UINT8
                        || loaded_type == DATA_UINT16 || loaded_type == DATA_UINT32
                        || loaded_type == DATA_CHAR || loaded_type == DATA_BOOL)) {
                    bool src_unsigned = (loaded_type == DATA_UINT
                                      || loaded_type == DATA_UINT8
                                      || loaded_type == DATA_UINT16
                                      || loaded_type == DATA_UINT32
                                      || loaded_type == DATA_CHAR
                                      || loaded_type == DATA_BOOL);
                    if (src_unsigned) {
                        fprintf(output, "  xor edx, edx\n");
                    } else {
                        fprintf(output, "  cdq\n");
                    }
                }

                cg_store(output, dest, dest_type,
                         (loaded_type == DATA_FLOAT || loaded_type == DATA_UFLOAT ||
                          loaded_type == DATA_FLOAT64 || loaded_type == DATA_UFLOAT64 ||
                          loaded_type == DATA_FLOAT8 || loaded_type == DATA_FLOAT16 ||
                          loaded_type == DATA_FLOAT32 || loaded_type == DATA_FLOAT128 ||
                          loaded_type == DATA_UFLOAT8 || loaded_type == DATA_UFLOAT16 ||
                          loaded_type == DATA_UFLOAT32 || loaded_type == DATA_UFLOAT128));
                break;
            }

            case IR_VAR_UNINIT:
                bool is_static = ir_temp->is_static_local;
                int var_stack_sz = (ir_temp->stack_bytes > 0) ? ir_temp->stack_bytes : 4;
                var_stack_sz = (var_stack_sz + 3) & ~3;
                if (in_function && !is_static) {
                    Manual_add_const(ir_temp->data_type, "", 0, ir_temp->result,
                                     TYPE_UNINIT_VAR, ir_temp->data_type);
                    int idx = get_const(ir_temp->result);
                    if (idx != -1) {
                        consts->storage[idx] = STORAGE_LOCAL;
                        current_local_offset -= var_stack_sz;
                        consts->offset[idx] = current_local_offset;
                        consts->is_array[idx] = false;
                        consts->array_size[idx] = 0;
                        consts->is_dynamic_array[idx] = false;
                        if (ir_temp->is_dynamic_array) {
                            consts->is_dynamic_array[idx] = true;
                        } else if (var_stack_sz > 4 &&
                                   ir_temp->data_type != DATA_INT64 &&
                                   ir_temp->data_type != DATA_UINT64 &&
                                   ir_temp->data_type != DATA_FLOAT64 &&
                                   ir_temp->data_type != DATA_UFLOAT64 &&
                                   ir_temp->data_type != DATA_STR) {
                            consts->is_array[idx] = true;
                            consts->array_size[idx] = var_stack_sz;
                        } else if (ir_temp->data_type == DATA_STR) {
                            consts->is_array[idx] = true;
                            consts->array_size[idx] = var_stack_sz;
                        }
                    }
                } else if (is_static) {
                    Manual_add_const(ir_temp->data_type, "", 0, ir_temp->result,
                                     TYPE_UNINIT_VAR, ir_temp->data_type);
                    int idx = get_const(ir_temp->result);
                    if (idx != -1) {
                        consts->storage[idx] = STORAGE_STATIC_LOCAL;
                        consts->offset[idx] = 0;
                    }
                } else {
                    Manual_add_const(ir_temp->data_type, "", 0, ir_temp->result,
                                     TYPE_UNINIT_VAR, ir_temp->data_type);
                    int idx = get_const(ir_temp->result);
                    if (idx != -1) {
                        consts->storage[idx] = STORAGE_GLOBAL;
                        consts->offset[idx] = 0;
                    }
                }
                const char* result_base_ = strip_ssa_suffix(ir_temp->result);
                if (pending_vararg_idx < pending_vararg_count
                    && pending_vararg_names
                    && result_base_
                    && strcmp(result_base_, pending_vararg_names[pending_vararg_idx]) == 0) {
                    int var_uninit_temp = pending_vararg_bytes_array[pending_vararg_idx];
                    int va_idx = get_const(ir_temp->result);
                    if (va_idx >= 0) {
                        consts->storage[va_idx] = STORAGE_LOCAL;
                        bool is_func_ptr = (ir_temp->data_type == DATA_INT
                                            && get_const(ir_temp->result) == va_idx
                                            && consts->is_array[va_idx] == false);
                        if (!is_func_ptr) {
                            current_local_offset -= 8;
                            consts->offset[va_idx] = current_local_offset;
                            int off = -consts->offset[va_idx];
                            fprintf(output, "  mov eax, dword [ebp+%d]\n", 8 + var_uninit_temp);
                            fprintf(output, "  mov dword [ebp-%d], eax\n", off);
                            fprintf(output, "  mov eax, dword [ebp+%d]\n", 8 + var_uninit_temp + 4);
                            fprintf(output, "  mov dword [ebp-%d], eax\n", off - 4);
                        } else {
                            current_local_offset -= 4;
                            consts->offset[va_idx] = current_local_offset;
                            int off = -consts->offset[va_idx];
                            fprintf(output, "  mov eax, dword [ebp+%d]\n", 8 + var_uninit_temp);
                            fprintf(output, "  mov dword [ebp-%d], eax\n", off);
                        }
                    }
                    pending_vararg_idx++;
                    if (pending_vararg_idx >= pending_vararg_count) {
                        pending_vararg_count = 0;
                        pending_vararg_idx = 0;
                    }
                }
                if (in_function && ir_temp->result
                    && strncmp(ir_temp->result, "_exc_", 5) == 0) {
                    int exc_idx = get_const(ir_temp->result);
                    if (exc_idx != -1 && consts->storage[exc_idx] == STORAGE_LOCAL) {
                        fprintf(output, "  mov dword [ebp-%d], 0\n",
                                -consts->offset[exc_idx]);
                    }
                }
                break;

            case IR_IF:
                break;

            case IR_JMPZ:
                cache_flush_all(output);
                char PZaddr_buf[100];
                if (cg_address(output, ir_temp->args1, PZaddr_buf, sizeof(PZaddr_buf))) {
                    fprintf(output, "  cmp DWORD %s, 0\n", PZaddr_buf);
                } else {
                    cg_load(output, ir_temp->args1, DATA_INT);
                    fprintf(output, "  cmp eax, 0\n");
                }
                if (is_local_label(ir_temp->args2))
                    fprintf(output, "  je .%s\n", ir_temp->args2);
                else
                    fprintf(output, "  je %s\n", ir_temp->args2);
                break;

            case IR_JMPNZ:
                cache_flush_all(output);
                if (ir_temp->args1 == NULL) {
                    INTERNAL_ERROR("codegen: IR_JMPNZ with NULL args1");
                }
                char PNZaddr_buf[100];
                if (cg_address(output, ir_temp->args1, PNZaddr_buf, sizeof(PNZaddr_buf))) {
                    fprintf(output, "  cmp DWORD %s, 0\n", PNZaddr_buf);
                } else {
                    cg_load(output, ir_temp->args1, DATA_INT);
                    fprintf(output, "  cmp eax, 0\n");
                }
                if (is_local_label(ir_temp->args2))
                    fprintf(output, "  jne .%s\n", ir_temp->args2);
                else
                    fprintf(output, "  jne %s\n", ir_temp->args2);
                break;

            case IR_JMP:
                cache_flush_all(output);
                if (is_local_label(ir_temp->args1))
                    fprintf(output, "  jmp .%s\n", ir_temp->args1);
                else
                    fprintf(output, "  jmp %s\n", ir_temp->args1);
                break;

            case IR_FORMAT: {
                cache_flush_all(output);
                has_format = true;
                has_memory_lib = true;
                int num = ir_temp->call_func_args_num;
                if (num > 0) {
                    for (int i = num - 1; i >= 0; i--) {
                        DataType t = ir_temp->call_func_args_type[i];
                        int code = (t == DATA_FLOAT || t == DATA_UFLOAT) ? 1 :
                                   (t == DATA_CHAR) ? 2 : (t == DATA_STRING) ? 3 :
                                   (t == DATA_BOOL) ? 4 : 0;
                        fprintf(output, "  push %d\n", code);
                    }
                    fprintf(output, "  mov ebx, esp\n");
                    for (int i = num - 1; i >= 0; i--) {
                        char* name = ir_temp->label[i];
                        if (name == NULL) {
                            FATAL(ERR_CG_UNKNOWN_VAR, LOC_NONE,
                                  "format argument %d has no value name", i);
                        }
                        if (name[0] == '.') {
                            int off = temp_map_get_offset(name);
                            fprintf(output, "  lea eax, [ebp-%d]\n", off);
                            fprintf(output, "  push eax\n");
                        } else {
                            int idx = get_const(name);
                            if (idx == -1) {
                                FATAL(ERR_CG_UNKNOWN_VAR, LOC_NONE, "variable '%s' not found", name);
                            }
                            switch (consts->storage[idx]) {
                                case STORAGE_GLOBAL:
                                    fprintf(output, "  push %s\n", name); break;
                                case STORAGE_PARAM:
                                    fprintf(output, "  lea eax, [ebp+%d]\n", consts->offset[idx]);
                                    fprintf(output, "  push eax\n");
                                    break;
                                case STORAGE_LOCAL:
                                    fprintf(output, "  lea eax, [ebp-%d]\n", -consts->offset[idx]);
                                    fprintf(output, "  push eax\n");
                                    break;
                            }
                        }
                    }
                    fprintf(output, "  push ebx\n");
                } else {
                    fprintf(output, "  push 0\n");
                }
                fprintf(output, "  push %d\n", num);
                char* fmt_name = ir_temp->args1;
                if (fmt_name[0] == '.') {
                    int off = temp_map_get_offset(fmt_name);
                    fprintf(output, "  push dword [ebp-%d]\n", off);
                } else {
                    int idx = get_const(fmt_name);
                    if (idx != -1) {
                        switch (consts->storage[idx]) {
                            case STORAGE_GLOBAL:
                                fprintf(output, "  push dword [%s]\n", fmt_name); break;
                            case STORAGE_PARAM:
                                fprintf(output, "  push dword [ebp+%d]\n", consts->offset[idx]); break;
                            case STORAGE_LOCAL:
                                fprintf(output, "  push dword [ebp-%d]\n", -consts->offset[idx]); break;
                        }
                    } else {
                        if (!is_temp(fmt_name) && get_const((char*)fmt_name) == -1 && !is_number(fmt_name)) {
                            char* label = add_const(DATA_STRING, (char*)fmt_name, 0, DATA_STRING);
                            fprintf(output, "  push %s\n", label);
                        } else {
                            fprintf(output, "  push %s\n", fmt_name);
                        }
                    }
                }
                fprintf(output, "  call _format_string\n");
                fprintf(output, "  add esp, %d\n", (num * 2 + 3) * 4);
                cache_flush_all(output);
                cg_store(output, ir_temp->result, DATA_STRING, false);
                break;
            }

            case IR_CALL: {
                cache_flush_all(output);
                if (ir_temp->args1 == NULL) {
                    ir_temp = ir_temp->next;
                    continue;
                }
                if (strcmp(ir_temp->args1, "_str_length") == 0 ||
                        strcmp(ir_temp->args1, "_str_char_at") == 0 ||
                        strcmp(ir_temp->args1, "_str_substring") == 0 ||
                        strcmp(ir_temp->args1, "_str_plus_char") == 0 ||
                        strcmp(ir_temp->args1, "_str_concat") == 0 ||
                        strcmp(ir_temp->args1, "_str_equal") == 0 ||
                        strcmp(ir_temp->args1, "_str_not_equal") == 0 ||
                        strcmp(ir_temp->args1, "_char_to_str") == 0 ||
                        strcmp(ir_temp->args1, "_char_concat") == 0 ||
                        strcmp(ir_temp->args1, "_char_pair_to_str") == 0 ||
                        strcmp(ir_temp->args1, "_str_plus_char") == 0 ||
                        strcmp(ir_temp->args1, "_char_plus_str") == 0 ||
                        strcmp(ir_temp->args1, "_str_substring") == 0 ||
                        strcmp(ir_temp->args1, "_str_plus_char2") == 0 ||
                        strcmp(ir_temp->args1, "_str_concat") == 0 ||
                        strcmp(ir_temp->args1, "_str_to_string") == 0 ||
                        strcmp(ir_temp->args1, "_str_substring_str") == 0 ||
                        strcmp(ir_temp->args1, "_str_concat_str") == 0 ||
                        strcmp(ir_temp->args1, "_str_plus_char_2") == 0 ||
                        strcmp(ir_temp->args1, "_char_plus_str_2") == 0 ) {
                    has_string_lib = true;
                    has_memory_lib = true;
                    has_gc = true;
                }

                if (strcmp(ir_temp->args1, "_GetCommandLineArgc") == 0 ||
                        strcmp(ir_temp->args1, "_GetCommandLineArgv") == 0 ||
                        strcmp(ir_temp->args1, "_system_exit") == 0 ||
                        strcmp(ir_temp->args1, "_system_sleep") == 0 ||
                        strcmp(ir_temp->args1, "_system_time") == 0 ||
                        strcmp(ir_temp->args1, "_system_getenv") == 0 ||
                        strcmp(ir_temp->args1, "_system_call") == 0 ||
                        strcmp(ir_temp->args1, "_sys_argv_at") == 0) {
                    has_system_lib = true;
                    has_memory_lib = true;
                }

                if (strcmp(ir_temp->args1, "_AllocMemory") == 0 ||
                    strcmp(ir_temp->args1, "_FreeMemory") == 0) {
                    has_memory_lib = true;
                }
                if (strcmp(ir_temp->args1, "_fgt") == 0 ||
                    strcmp(ir_temp->args1, "_fge") == 0 ||
                    strcmp(ir_temp->args1, "_feq") == 0 ||
                    strcmp(ir_temp->args1, "_fne") == 0 ||
                    strcmp(ir_temp->args1, "_fle") == 0 ||
                    strcmp(ir_temp->args1, "_flt") == 0 ||
                    strcmp(ir_temp->args1, "_fmod_fpu") == 0 ||
                    strcmp(ir_temp->args1, "_float_to_bits") == 0 ||
                    strcmp(ir_temp->args1, "_bits_to_float") == 0) {
                    has_float_lib = true;
                }
                if (strcmp(ir_temp->args1, "_array_to_string") == 0) {
                    has_array_lib = true;
                }
                if (strcmp(ir_temp->args1, "_print") == 0 ||
                    strcmp(ir_temp->args1, "_set_float_mode") == 0 ||
                    strcmp(ir_temp->args1, "_set_float_precision") == 0) {
                    has_print = true;
                }
                if (strcmp(ir_temp->args1, "_format_string") == 0) {
                    has_format = true;
                    has_memory_lib = true;
                }
                if (strcmp(ir_temp->args1, "_input_parse") == 0) {
                    has_input = true;
                    has_memory_lib = true;
                }
                if (strcmp(ir_temp->args1, "_array_bounds_error") == 0) {
                    has_bounds_check = true;
                }
                if (semantics_is_native_symbol(ir_temp->args1)
                    && strncmp(ir_temp->args1, "File_", 5) == 0) {
                    has_file_lib = true;
                    has_gc = true;
                    has_memory_lib = true;
                    bool already = false;
                    for (int fi = 0; fi < file_extern_count; fi++) {
                        if (strcmp(file_extern_names[fi], ir_temp->args1) == 0) {
                            already = true;
                            break;
                        }
                    }
                    if (!already && file_extern_count < 64) {
                        file_extern_names[file_extern_count++] =
                            strdup(ir_temp->args1);
                    }
                }
                if (strncmp(ir_temp->args1, "_socket_", 8) == 0 ||
                    strncmp(ir_temp->args1, "_iocp_", 6) == 0 ||
                    strncmp(ir_temp->args1, "_ov_", 4) == 0) {
                    has_socket_lib = true;
                    has_gc = true;
                    has_memory_lib = true;
                }
                if (strcmp(ir_temp->args1, "Canvas_load_string") == 0) {
                    has_image_lib = true;
                    has_window_lib = true;
                }
                if (strncmp(ir_temp->args1, "Music_", 6) == 0) {
                    has_music_lib = true;
                }
                if (strncmp(ir_temp->args1, "Video_", 6) == 0) {
                    has_video_lib = true;
                    has_window_lib = true;
                    has_music_lib = true;
                }
                if (strncmp(ir_temp->args1, "Window_", 7) == 0 ||
                    strncmp(ir_temp->args1, "Canvas_", 7) == 0 ||
                    strncmp(ir_temp->args1, "Button_", 7) == 0 ||
                    strncmp(ir_temp->args1, "TextBox_", 8) == 0 ||
                    strncmp(ir_temp->args1, "Label_", 6) == 0 ||
                    strncmp(ir_temp->args1, "CheckBox_", 9) == 0 ||
                    strncmp(ir_temp->args1, "RadioButton_", 12) == 0 ||
                    strncmp(ir_temp->args1, "ComboBox_", 9) == 0 ||
                    strncmp(ir_temp->args1, "ListBox_", 8) == 0 ||
                    strncmp(ir_temp->args1, "GroupBox_", 9) == 0 ||
                    strncmp(ir_temp->args1, "Panel_", 6) == 0 ||
                    strncmp(ir_temp->args1, "Menu_", 5) == 0 ||
                    strncmp(ir_temp->args1, "Toolbar_", 8) == 0 ||
                    strncmp(ir_temp->args1, "ButtonBar_", 10) == 0 ||
                    strncmp(ir_temp->args1, "StatusBar_", 10) == 0 ||
                    strncmp(ir_temp->args1, "ProgressBar_", 12) == 0 ||
                    strncmp(ir_temp->args1, "SpinBox_", 8) == 0 ||
                    strncmp(ir_temp->args1, "Slider_", 7) == 0 ||
                    strncmp(ir_temp->args1, "_win_", 5) == 0) {
                    has_window_lib = true;
                    has_image_lib = true;
                    has_gc = true;
                }
                if (strncmp(ir_temp->args1, "Time_", 5) == 0) {
                    has_time_lib = true;
                }
                if (strcmp(ir_temp->args1, "_thread_create") == 0 ||
                    strcmp(ir_temp->args1, "_thread_join") == 0 ||
                    strcmp(ir_temp->args1, "_thread_self") == 0 ||
                    strcmp(ir_temp->args1, "_thread_sleep") == 0) {
                    has_thread_lib = true;
                }
                if (strncmp(ir_temp->args1, "Function_", 9) == 0) {
                    has_function_lib = true;
                }
                if (strncmp(ir_temp->args1, "_sync_", 6) == 0) {
                    has_sync_lib = true;
                }
                if (strcmp(ir_temp->args1, "_error_raise") == 0 ||
                    strcmp(ir_temp->args1, "_exc_frame_push") == 0 ||
                    strcmp(ir_temp->args1, "_exc_frame_pop") == 0 ||
                    strcmp(ir_temp->args1, "_exc_get_current") == 0 ||
                    strcmp(ir_temp->args1, "_error_is_instance") == 0 ||
                    strcmp(ir_temp->args1, "_exc_setjmp") == 0) {
                    has_exception_lib = true;
                    has_print = true;
                }
                if (strcmp(ir_temp->args1, "_unsafe_alloc") == 0) {
                    fprintf(output, "  call _unsafe_alloc\n");
                    has_unsafe_lib = true;
                    has_memory_lib = true;
                } else if (strcmp(ir_temp->args1, "_unsafe_free") == 0) {
                    fprintf(output, "  call _unsafe_free\n");
                    has_unsafe_lib = true;
                    has_memory_lib = true;
                } else if (strcmp(ir_temp->args1, "_AllocMemory") == 0) {
                    fprintf(output, "  call _gc_alloc\n");
                    has_gc = true;
                    has_memory_lib = true;
                } else {
                    fprintf(output, "  call %s\n", ir_temp->args1);
                }
                cache_flush_all(output);
                if (ir_temp->total_push_bytes > 0) {
                    fprintf(output, "  add esp, %d\n", ir_temp->total_push_bytes);
                } else if (ir_temp->call_func_args_num > 0) {
                    fprintf(output, "  add esp, %d\n", ir_temp->call_func_args_num * 4);
                }
                if (ir_temp->temp_stack_alloc > 0) {
                    fprintf(output, "  add esp, %d   ; cleanup temp str alloc\n",
                            ir_temp->temp_stack_alloc);
                }
                if (ir_temp->result) {
                    bool is_float_call = (ir_temp->data_type == DATA_FLOAT || ir_temp->data_type == DATA_UFLOAT ||
                                          ir_temp->data_type == DATA_FLOAT64 || ir_temp->data_type == DATA_UFLOAT64 ||
                                          ir_temp->data_type == DATA_FLOAT8 || ir_temp->data_type == DATA_FLOAT16 ||
                                          ir_temp->data_type == DATA_FLOAT32 || ir_temp->data_type == DATA_FLOAT128 ||
                                          ir_temp->data_type == DATA_UFLOAT8 || ir_temp->data_type == DATA_UFLOAT16 ||
                                          ir_temp->data_type == DATA_UFLOAT32 || ir_temp->data_type == DATA_UFLOAT128);
                    cg_store(output, ir_temp->result, ir_temp->data_type, is_float_call);
                    temp_map_add(ir_temp->result, ir_temp->data_type);
                }
                break;
            }

            case IR_CALL_INDIRECT: {
                cache_flush_all(output);
                if (ir_temp->args1 == NULL || strcmp(ir_temp->args1, "(null)") == 0) {
                    fprintf(output, "  ; ERROR: indirect call target is null\n");
                    fprintf(output, "  xor eax, eax\n");
                } else {
                    int idx = get_const(ir_temp->args1);
                    if (idx != -1 && consts->storage[idx] != STORAGE_STATIC_LOCAL
                        && consts->storage[idx] != STORAGE_STATIC_GUARD) {
                        char addr[128];
                        switch (consts->storage[idx]) {
                            case STORAGE_GLOBAL:
                                snprintf(addr, sizeof(addr), "dword [%s]",
                                         consts->name[idx] ? consts->name[idx] : ir_temp->args1);
                                break;
                            case STORAGE_PARAM:
                                snprintf(addr, sizeof(addr), "dword [ebp+%d]",
                                         consts->offset[idx]);
                                break;
                            case STORAGE_LOCAL:
                                snprintf(addr, sizeof(addr), "dword [ebp-%d]",
                                         -consts->offset[idx]);
                                break;
                            default:
                                snprintf(addr, sizeof(addr), "dword [%s]",
                                         ir_temp->args1);
                                break;
                        }
                        fprintf(output, "  mov eax, %s\n", addr);
                        } else {
                            DataType loaded_type2 = cg_load(output, ir_temp->args1, DATA_INT);
                            (void)loaded_type2;
                        }
                }
                fprintf(output, "  call eax\n");
                cache_flush_all(output);
                if (ir_temp->total_push_bytes > 0) {
                    fprintf(output, "  add esp, %d\n", ir_temp->total_push_bytes);
                } else if (ir_temp->call_func_args_num > 0) {
                    fprintf(output, "  add esp, %d\n", ir_temp->call_func_args_num * 4);
                }
                if (ir_temp->temp_stack_alloc > 0) {
                    fprintf(output, "  add esp, %d\n",
                            ir_temp->temp_stack_alloc);
                }
                if (ir_temp->result) {
                    bool is_float_call = (ir_temp->data_type == DATA_FLOAT || ir_temp->data_type == DATA_UFLOAT ||
                                          ir_temp->data_type == DATA_FLOAT64 || ir_temp->data_type == DATA_UFLOAT64 ||
                                          ir_temp->data_type == DATA_FLOAT8 || ir_temp->data_type == DATA_FLOAT16 ||
                                          ir_temp->data_type == DATA_FLOAT32 || ir_temp->data_type == DATA_FLOAT128 ||
                                          ir_temp->data_type == DATA_UFLOAT8 || ir_temp->data_type == DATA_UFLOAT16 ||
                                          ir_temp->data_type == DATA_UFLOAT32 || ir_temp->data_type == DATA_UFLOAT128);
                    cg_store(output, ir_temp->result, ir_temp->data_type, is_float_call);
                    temp_map_add(ir_temp->result, ir_temp->data_type);
                }
                break;
            }

            case IR_LABEL: {
                const char* label_name = ir_temp->args1;
                if (main_has_args && strcmp(label_name, "_main") == 0) {
                    fprintf(output, "_usermain:\n");
                } else if (is_local_label(ir_temp->args1)) {
                    fprintf(output, ".%s:\n", ir_temp->args1);
                } else {
                    fprintf(output, "%s:\n", label_name);
                }

                cache_flush_all(output);

                if (ir_temp->has_return) {
                    current_regmap = regmap_lookup_function(ir_temp->args1);
                    cache_flush_all(output);
                    scan_function_escapes(ir_temp);
                    pending_vararg_count = 0;
                    pending_vararg_idx = 0;
                    pending_vararg_names = NULL;
                    pending_vararg_bytes_array = NULL;
                    temp_map.count = 0;
                    int keep = 0;
                    for (int k = 0; k < consts->index; k++) {
                        if (consts->storage[k] == STORAGE_GLOBAL ||
                            consts->storage[k] == STORAGE_STATIC_LOCAL ||
                            consts->storage[k] == STORAGE_STATIC_GUARD ||
                            consts->var_type[k] == TYPE_CONST) {
                            if (keep != k) {
                                consts->type[keep] = consts->type[k];
                                consts->value[keep] = consts->value[k];
                                consts->name[keep] = consts->name[k];
                                consts->var_type[keep] = consts->var_type[k];
                                consts->value_type[keep] = consts->value_type[k];
                                consts->storage[keep] = consts->storage[k];
                                consts->offset[keep] = consts->offset[k];
                                consts->is_array[keep] = consts->is_array[k];
                                consts->array_size[keep] = consts->array_size[k];
                                consts->is_dynamic_array[keep] = consts->is_dynamic_array[k];
                                consts->value[k] = NULL;
                                consts->name[k] = NULL;
                            }
                            keep++;
                            } else {
                                free(consts->value[k]);
                                free(consts->name[k]);
                                consts->value[k] = NULL;
                                consts->name[k] = NULL;
                                consts->storage[k] = STORAGE_LOCAL;
                                consts->offset[k] = 0;
                                consts->is_array[k] = false;
                                consts->array_size[k] = 0;
                                consts->is_dynamic_array[k] = false;
                            }
                    }
                    consts->index = keep;
                    for (int k = 0; k < keep; k++) {
                        if (consts->storage[k] == STORAGE_PARAM
                            || consts->storage[k] == STORAGE_LOCAL) {
                            consts->is_array[k] = false;
                            }
                    }
                    IR* scan = ir_temp;
                    while (scan != NULL)
                    {
                        if (scan != ir_temp && scan->type == IR_LABEL && scan->has_return)
                        {
                            break;
                        }
                        if (scan->result && is_temp(scan->result))
                        {
                            DataType add_type = scan->data_type;
                            if (scan->type == IR_LITERAL && scan->args1 && is_number(scan->args1)) {
                                long long v64 = parse_integer64(scan->args1);
                                if (v64 > 2147483647LL || v64 < -2147483648LL) {
                                    add_type = DATA_INT64;
                                }
                            }
                            temp_map_add(scan->result, add_type);
                        }
                        if (scan->args1 && is_temp(scan->args1)) {
                            temp_map_add(scan->args1, DATA_INT);
                        }
                        if (scan->args2 && is_temp(scan->args2)) {
                            temp_map_add(scan->args2, DATA_INT);
                        }
                        scan = scan->next;
                    }

                    current_function = ir_temp->args1;
                    current_param_offset = 8;
                    current_param_index = 0;
                    in_function = true;
                    current_func_end = find_func_last_ret(ir_temp);

                    cg_func_has_hidden_ret = false;
                    if (ir_temp->next
                        && ir_temp->next->type == IR_PARAM_DEF
                        && ir_temp->next->result
                        && ir_temp->next->result[0] == '.'
                        && ir_temp->next->result[1] == 'r'
                        && isdigit((unsigned char)ir_temp->next->result[2])) {
                        cg_func_has_hidden_ret = true;
                    }

                    int max_temp_idx = -1;
                    int local_var_count = 0;
                    int total_array_bytes = 0;
                    scan = ir_temp->next;
                    while (scan) {
                        if (scan->type == IR_LABEL && scan->has_return) break;

                        if (scan->result && scan->result[0] == '.'
                            && scan->result[1] == 't'
                            && isdigit((unsigned char)scan->result[2])) {
                            int idx = atoi(scan->result + 2);
                            if (idx > max_temp_idx) max_temp_idx = idx;
                            }

                        if (scan->args1 && scan->args1[0] == '.'
                            && scan->args1[1] == 't'
                            && isdigit((unsigned char)scan->args1[2])) {
                            int idx = atoi(scan->args1 + 2);
                            if (idx > max_temp_idx) max_temp_idx = idx;
                            }

                        if (scan->args2 && scan->args2[0] == '.'
                            && scan->args2[1] == 't'
                            && isdigit((unsigned char)scan->args2[2])) {
                            int idx = atoi(scan->args2 + 2);
                            if (idx > max_temp_idx) max_temp_idx = idx;
                            }

                        if (scan->type == IR_VAR || scan->type == IR_VAR_UNINIT)
                        {
                            local_var_count++;
                            if (scan->stack_bytes > 4)
                                total_array_bytes += scan->stack_bytes;
                        }
                        if (scan->result && strncmp(scan->result, "_foreach_idx", 12) == 0)
                            local_var_count++;

                        if (scan->type == IR_ARRAY_DECL)
                            total_array_bytes += scan->array_size * scan->elem_size;
                        if (scan->type == IR_ALLOC_ARRAY && scan->stack_bytes > 0
                            && scan->result && scan->result[0]) {
                            total_array_bytes += (scan->stack_bytes + 3) & ~3;
                            }
                        scan = scan->next;
                    }
                    scan = ir_temp->next;
                    while (scan) {
                        if (scan->type == IR_LABEL && scan->has_return) break;
                        if (scan->type == IR_ALLOC_ARRAY && scan->stack_bytes > 0
                            && scan->result && scan->result[0]) {
                            total_array_bytes += (scan->stack_bytes + 3) & ~3;
                            }
                        scan = scan->next;
                    }
                    int func_temp_count = ir_temp->func_temp_count;
                    if (func_temp_count < max_temp_idx + 1)
                        func_temp_count = max_temp_idx + 1;
                    temp_map_compute_offsets();
                    int temp_area_size = 0;
                    for (int ti = 0; ti < temp_map.count; ti++) {
                        int off = temp_map.offsets[ti];
                        int sz = get_type_size(temp_map.types[ti]);
                        if (sz < 4) sz = 4;
                        int end = off + sz;
                        if (end > temp_area_size) temp_area_size = end;
                    }
                    if (temp_area_size < (func_temp_count + 16) * 4)
                        temp_area_size = (func_temp_count + 16) * 4;
                    temp_area_size = (temp_area_size + 3) & ~3;
                    int local_vars_bytes = local_var_count * 4;
                    int spill_slots_needed = 0;
                    if (current_regmap) {
                        for (int i = 0; i < current_regmap->count; i++)
                            if (current_regmap->entries[i].reg == R_NONE)
                                spill_slots_needed++;
                    }
                    spill_base = temp_area_size + local_vars_bytes
                               + total_array_bytes + 8;
                    int stack_size = spill_base + spill_slots_needed * 4;
                    stack_size = (stack_size + 15) & ~15;
                    if (ir_temp->vararg_internal_name_ir != NULL) {
                        total_array_bytes += 8;
                    }
                    current_local_offset = -(temp_area_size + 8);

                    current_func_callee_mask = regmap_callee_saved_mask(g_regmap);
                    fprintf(output, "  push ebp\n");
                    fprintf(output, "  mov ebp, esp\n");
                    {
                        fprintf(output, "  push ebx\n");
                        fprintf(output, "  push esi\n");
                        fprintf(output, "  push edi\n");
                    }
                    fprintf(output, "  sub esp, %d\n", stack_size);
                    if (stack_size > 0) {
                        int zwords = stack_size / 4;
                        fprintf(output, "  lea edi, [ebp-%d]\n", stack_size);
                        fprintf(output, "  xor eax, eax\n");
                        fprintf(output, "  mov ecx, %d\n", zwords);
                        fprintf(output, "  cld\n");
                        fprintf(output, "  rep stosd\n");
                    }

                    if (ir_temp->vararg_count > 0 && ir_temp->vararg_internal_names_array) {
                        pending_vararg_count = ir_temp->vararg_count;
                        pending_vararg_idx = 0;
                        pending_vararg_names = ir_temp->vararg_internal_names_array;
                        pending_vararg_bytes_array = ir_temp->vararg_bytes_array;
                    }

                    if (strcmp(ir_temp->args1, "_main") == 0 && !main_has_args) {
                        fprintf(output, "  push 65001\n");
                        fprintf(output, "  call _SetConsoleOutputCP@4\n");
                        fprintf(output, "  finit\n");
                        fprintf(output, "  call _gc_init\n");
                        in_main = true;
                        has_gc = true;
                        has_memory_lib = true;
                    }
                }
                break;
            }

        case IR_PARAM_DEF:
            int param_bytes;
            if (ir_temp->push_bytes > 0) {
                param_bytes = ir_temp->push_bytes;
            } else if (ir_temp->data_type == DATA_STR) {
                param_bytes = 8;
            } else {
                param_bytes = 4;
            }
            int saved_offset = current_param_offset;
            if (ir_temp->data_type == DATA_STR && current_param_index == 0) {
                saved_offset = 8;
            }
            if (main_has_args && current_function
                && strcmp(current_function, "_main") == 0) {
                param_bytes = 4;
            }
            bool param_is_dynamic = ir_temp->is_dynamic_array
                                    || (ir_temp->is_array_param
                                        && ir_temp->push_bytes <= 8
                                        && ir_temp->data_type != DATA_INT64
                                        && ir_temp->data_type != DATA_UINT64
                                        && ir_temp->data_type != DATA_FLOAT64
                                        && ir_temp->data_type != DATA_UFLOAT64
                                        && ir_temp->data_type != DATA_STR);
            add_symbol(ir_temp->data_type, "", ir_temp->result,
                     TYPE_VAR, ir_temp->data_type,
                     STORAGE_PARAM, saved_offset,
                     ir_temp->is_array_param,
                     ir_temp->is_array_param ? ir_temp->array_size : 0,
                     param_is_dynamic);
            const char* rn = ir_temp->result;
            if (rn && rn[0] == '.' && rn[1] == 'r'
                && isdigit((unsigned char)rn[2])) {
                int ridx = get_const(rn);
                if (ridx >= 0) {
                    consts->is_array[ridx] = false;
                    consts->is_dynamic_array[ridx] = false;
                }
            }
            current_param_offset += param_bytes;
            current_param_index++;
            const char* result_base = strip_ssa_suffix(ir_temp->result);
            if (pending_vararg_idx < pending_vararg_count
               && pending_vararg_names
               && result_base
               && strcmp(result_base, pending_vararg_names[pending_vararg_idx]) == 0) {
                int param_def_temp = pending_vararg_bytes_array[pending_vararg_idx];
                int va_idx = get_const(ir_temp->result);
                if (va_idx >= 0) {
                    consts->storage[va_idx] = STORAGE_LOCAL;
                    bool is_func_ptr = (ir_temp->data_type != DATA_STR
                                        && get_const(ir_temp->result) == va_idx
                                        && !ir_temp->is_array_param);
                    if (!is_func_ptr) {
                        current_local_offset -= 8;
                        consts->offset[va_idx] = current_local_offset;
                        int off = -consts->offset[va_idx];
                        fprintf(output, "  mov eax, dword [ebp+%d]\n", 8 + param_def_temp);
                        fprintf(output, "  mov dword [ebp-%d], eax\n", off);
                        fprintf(output, "  mov eax, dword [ebp+%d]\n", 8 + param_def_temp + 4);
                        fprintf(output, "  mov dword [ebp-%d], eax\n", off - 4);
                    } else {
                        current_local_offset -= 4;
                        consts->offset[va_idx] = current_local_offset;
                        int off = -consts->offset[va_idx];
                        fprintf(output, "  mov eax, dword [ebp+%d]\n", 8 + param_def_temp);
                        fprintf(output, "  mov dword [ebp-%d], eax\n", off);
                    }
                }
                pending_vararg_idx++;
                if (pending_vararg_idx >= pending_vararg_count) {
                    pending_vararg_count = 0;
                    pending_vararg_idx = 0;
                }
            }
            break;

        case IR_RET:
            cache_flush_all(output);
            if (in_main) {
                if (ir_temp->has_return) {
                    char* ret_val = ir_temp->args1;
                    if (ret_val != NULL && is_number(ret_val) && strchr(ret_val, '.') != NULL)
                    {
                        char* label = add_const(DATA_FLOAT, ret_val, 0, DATA_FLOAT);
                        fprintf(output, "  fld dword [%s]\n", label);
                    } else if (is_temp(ret_val)) {
                        int off = temp_map_get_offset(ret_val);
                        fprintf(output, "  mov eax, dword [ebp-%d]\n", off);
                    } else if (get_const(ret_val) != -1) {
                        int idx = get_const(ret_val);
                        char addr[100];
                        switch (consts->storage[idx]) {
                        case STORAGE_GLOBAL: sprintf(addr, "[%s]", ret_val); break;
                        case STORAGE_PARAM: sprintf(addr, "[ebp+%d]", consts->offset[idx]); break;
                        case STORAGE_LOCAL: sprintf(addr, "[ebp-%d]", -consts->offset[idx]); break;
                        }
                        fprintf(output, "  mov eax, dword %s\n", addr);
                    } else {
                        fprintf(output, "  mov eax, %s\n", ret_val);
                    }
                } else {
                    fprintf(output, "  xor eax, eax\n");
                }
                fprintf(output, "  push eax\n");
                fprintf(output, "  call _ExitProcess@4\n");
                in_main = false;
            } else
            {
                if (ir_temp->has_return) {
                    char* ret_val = ir_temp->args1;
                    DataType ret_type = ir_temp->data_type;
                    if (ret_type == DATA_FLOAT || ret_type == DATA_UFLOAT) {
                        if (is_temp(ret_val)) {
                            int off = temp_map_get_offset(ret_val);
                            fprintf(output, "  fld dword [ebp-%d]\n", off);
                        } else {
                            int idx = get_const(ret_val);
                            if (idx != -1) {
                                char addr[100];
                                switch (consts->storage[idx]) {
                                case STORAGE_LOCAL:
                                    sprintf(addr, "[ebp-%d]", -consts->offset[idx]); break;
                                case STORAGE_PARAM:
                                    sprintf(addr, "[ebp+%d]", consts->offset[idx]); break;
                                case STORAGE_GLOBAL:
                                    sprintf(addr, "[%s]", ret_val); break;
                                default: sprintf(addr, "[%s]", ret_val); break;
                                }
                                fprintf(output, "  fld dword %s\n", addr);
                            } else {
                                char* label = add_const(DATA_FLOAT, ret_val, 0, DATA_FLOAT);
                                fprintf(output, "  fld dword [%s]\n", label);
                            }
                        }
                    } else {
                        int dyn_idx = ret_val ? get_const(ret_val) : -1;
                        bool ret_dyn_array = (dyn_idx != -1
                                              && consts->is_dynamic_array[dyn_idx]);
                        if (ret_dyn_array) {
                            char dyn_addr[100];
                            switch (consts->storage[dyn_idx]) {
                            case STORAGE_LOCAL:
                                snprintf(dyn_addr, sizeof(dyn_addr), "[ebp-%d]",
                                         -consts->offset[dyn_idx]);
                                break;
                            case STORAGE_PARAM:
                                snprintf(dyn_addr, sizeof(dyn_addr), "[ebp+%d]",
                                         consts->offset[dyn_idx]);
                                break;
                            default:
                                snprintf(dyn_addr, sizeof(dyn_addr), "[%s]",
                                         consts->name[dyn_idx]
                                             ? consts->name[dyn_idx]
                                             : ret_val);
                                break;
                            }
                            fprintf(output, "  mov eax, dword %s\n", dyn_addr);
                        } else {
                            cg_load(output, ret_val, ret_type);
                        }
                        if (ret_type == DATA_INT64 || ret_type == DATA_UINT64) {
                            fprintf(output, "  ; edx:eax already holds int64 return value\n");
                        }
                    }
                }

                if (ir_temp->ret_has_hidden_ptr) {
                    fprintf(output, "  mov eax, [ebp+8]\n");
                }

                cache_flush_all(output);
                fprintf(output, "  pop edi\n");
                fprintf(output, "  pop esi\n");
                fprintf(output, "  pop ebx\n");
                fprintf(output, "  leave\n");
                fprintf(output, "  ret\n");
            }
            if (ir_temp == current_func_end) {
                in_function = false;
                current_func_end = NULL;
            }
            break;

        case IR_ALLOC_CLASS: {
            cache_flush_all(output);
            fprintf(output, "  push %s\n", ir_temp->args1);
            fprintf(output, "  call _gc_alloc\n");
            fprintf(output, "  add esp, 4\n");
            cache_flush_all(output);
            if (ir_temp->class_id > 0) {
                fprintf(output, "  mov dword [eax], %d\n", ir_temp->class_id);
            } else {
                fprintf(output, "  mov dword [eax], 0\n");
            }
            if (ir_temp->vtable_label && ir_temp->vtable_label[0]) {
                fprintf(output, "  mov dword [eax+4], %s\n",
                        ir_temp->vtable_label);
            } else {
                fprintf(output, "  mov dword [eax+4], 0\n");
            }
            cg_store(output, ir_temp->result, DATA_INT, false);
            has_memory_lib = true;
            has_gc = true;
            break;
        }

        case IR_LOAD_MEMBER: {
            cache_flush_all(output);
            int mem_off = atoi(ir_temp->args2);

            cg_load(output, ir_temp->args1, DATA_INT);
            fprintf(output, "  test eax, eax\n");
            fprintf(output, "  jz _null_ptr_error\n");
            has_bounds_check = true;

            if (ir_temp->data_type == DATA_FLOAT || ir_temp->data_type == DATA_UFLOAT) {
                fprintf(output, "  fld dword [eax+%d]\n", mem_off);
                cg_store(output, ir_temp->result, ir_temp->data_type, true);
            } else if (ir_temp->data_type == DATA_FLOAT64 || ir_temp->data_type == DATA_UFLOAT64) {
                fprintf(output, "  fld qword [eax+%d]\n", mem_off);
                cg_store(output, ir_temp->result, ir_temp->data_type, true);
            } else if (ir_temp->data_type == DATA_INT64 || ir_temp->data_type == DATA_UINT64) {
                fprintf(output, "  mov ecx, dword [eax+%d]\n", mem_off + 4);
                fprintf(output, "  mov eax, dword [eax+%d]\n", mem_off);
                cache_flush_reg(output, R_EDX);
                fprintf(output, "  mov edx, ecx\n");
                cg_store(output, ir_temp->result, ir_temp->data_type, false);
            } else if (ir_temp->data_type == DATA_CHAR || ir_temp->data_type == DATA_BOOL) {
                fprintf(output, "  movzx eax, byte [eax+%d]\n", mem_off);
                cg_store(output, ir_temp->result, ir_temp->data_type, false);
            } else {
                fprintf(output, "  mov eax, dword [eax+%d]\n", mem_off);
                cg_store(output, ir_temp->result, ir_temp->data_type, false);
            }
            if (ir_temp->result && is_temp(ir_temp->result)) {
                temp_map_add(ir_temp->result, ir_temp->data_type);
            }
            break;
        }

        case IR_LOAD_THIS: {
            int this_offset = cg_func_has_hidden_ret ? 12 : 8;
            fprintf(output, "  mov eax, [ebp+%d]\n", this_offset);
            cg_store(output, ir_temp->result, DATA_INT, false);
            if (ir_temp->result && is_temp(ir_temp->result)) {
                temp_map_add(ir_temp->result, DATA_INT);
            }
            break;
        }

        case IR_BOUNDS_CHECK:{
            cache_flush_all(output);
            char* idx = ir_temp->args1;
            char* dim = ir_temp->args2;

            if (is_temp(idx)) {
                int off = temp_map_get_offset(idx);
                fprintf(output, "  mov eax, dword [ebp-%d]\n", off);
            } else {
                int idx_const = get_const(idx);
                if (idx_const != -1) {
                    char addr[100];
                    switch (consts->storage[idx_const]) {
                    case STORAGE_GLOBAL: sprintf(addr, "[%s]", idx); break;
                    case STORAGE_PARAM:  sprintf(addr, "[ebp+%d]", consts->offset[idx_const]); break;
                    case STORAGE_LOCAL:  sprintf(addr, "[ebp-%d]", -consts->offset[idx_const]); break;
                    }
                    fprintf(output, "  mov eax, dword %s\n", addr);
                } else {
                    fprintf(output, "  mov eax, %s\n", idx);
                }
            }

            if (is_temp(dim)) {
                int off = temp_map_get_offset(dim);
                fprintf(output, "  mov ebx, dword [ebp-%d]\n", off);
            } else {
                int dim_const = get_const(dim);
                if (dim_const != -1) {
                    char addr[100];
                    switch (consts->storage[dim_const]) {
                    case STORAGE_GLOBAL: sprintf(addr, "[%s]", dim); break;
                    case STORAGE_PARAM:  sprintf(addr, "[ebp+%d]", consts->offset[dim_const]); break;
                    case STORAGE_LOCAL:  sprintf(addr, "[ebp-%d]", -consts->offset[dim_const]); break;
                    }
                    fprintf(output, "  mov ebx, dword %s\n", addr);
                } else {
                    fprintf(output, "  mov ebx, %s\n", dim);
                }
            }

            fprintf(output, "  cmp eax, ebx\n");
            fprintf(output, "  jge _array_bounds_error\n");
            fprintf(output, "  cmp eax, 0\n");
            fprintf(output, "  jl _array_bounds_error\n");

            has_bounds_check = true;
            break;
        }

        case IR_COPY_BLOCK: {
            cache_flush_all(output);
            char* dst_ptr = ir_temp->args1;
            char* src_ptr = ir_temp->args2;
            int byte_count = atoi(ir_temp->result);

            if (is_temp(dst_ptr)) {
                int off = temp_map_get_offset(dst_ptr);
                fprintf(output, "  mov edi, dword [ebp-%d]\n", off);
            } else {
                int idx = get_const(dst_ptr);
                if (idx != -1) {
                    if (consts->storage[idx] == STORAGE_PARAM
                        && !consts->is_array[idx]) {
                        fprintf(output, "  mov edi, dword [ebp+%d]\n",
                                consts->offset[idx]);
                    } else {
                        char addr[100];
                        cg_address(output, dst_ptr, addr, sizeof(addr));
                        fprintf(output, "  lea edi, %s\n", addr);
                    }
                } else {
                    cg_load(output, dst_ptr, DATA_INT);
                    fprintf(output, "  mov edi, eax\n");
                }
            }

            if (byte_count == 8) {
                int src_idx = is_temp(src_ptr) ? -1 : get_const(src_ptr);
                if (is_temp(src_ptr)) {
                    DataType st = temp_map_get((char*)src_ptr);
                    if (st == DATA_ERROR) st = DATA_INT;
                    if (get_type_size(st) == 8) {
                        int off = temp_map_get_offset(src_ptr);
                        fprintf(output, "  mov eax, dword [ebp-%d]\n", off);
                        fprintf(output, "  mov dword [edi], eax\n");
                        fprintf(output, "  mov eax, dword [ebp-%d]\n", off - 4);
                        fprintf(output, "  mov dword [edi+4], eax\n");
                    } else {
                        cg_load(output, src_ptr, DATA_INT);
                        fprintf(output, "  mov esi, eax\n");
                        fprintf(output, "  mov ecx, 2\n");
                        fprintf(output, "  cld\n");
                        fprintf(output, "  rep movsd\n");
                    }
                } else if (src_idx != -1
                           && !consts->is_array[src_idx]
                           && get_type_size(consts->type[src_idx]) == 8) {
                    char saddr[100];
                    if (cg_address(output, src_ptr, saddr, sizeof(saddr))) {
                        size_t alen = strlen(saddr);
                        if (alen > 0 && saddr[alen-1] == ']') {
                            saddr[alen-1] = '\0';
                            fprintf(output, "  mov eax, dword %s]\n", saddr);
                            fprintf(output, "  mov dword [edi], eax\n");
                            fprintf(output, "  mov eax, dword %s+4]\n", saddr);
                            fprintf(output, "  mov dword [edi+4], eax\n");
                        } else {
                            fprintf(output, "  mov eax, dword [%s]\n", saddr);
                            fprintf(output, "  mov dword [edi], eax\n");
                            fprintf(output, "  mov eax, dword [%s+4]\n", saddr);
                            fprintf(output, "  mov dword [edi+4], eax\n");
                        }
                    } else {
                        cg_load(output, src_ptr, DATA_INT64);
                        fprintf(output, "  mov dword [edi], eax\n");
                        fprintf(output, "  mov dword [edi+4], edx\n");
                    }
                } else {
                    cg_load(output, src_ptr, DATA_INT);
                    fprintf(output, "  mov esi, eax\n");
                    fprintf(output, "  mov ecx, 2\n");
                    fprintf(output, "  cld\n");
                    fprintf(output, "  rep movsd\n");
                }
                break;
            }

            if (is_temp(src_ptr)) {
                int off = temp_map_get_offset(src_ptr);
                fprintf(output, "  mov esi, dword [ebp-%d]\n", off);
            } else {
                int idx = get_const(src_ptr);
                if (idx != -1 && consts->is_array[idx]) {
                    char addr[100];
                    cg_address(output, src_ptr, addr, sizeof(addr));
                    fprintf(output, "  lea esi, %s\n", addr);
                } else {
                    cg_load(output, src_ptr, DATA_INT);
                    fprintf(output, "  mov esi, eax\n");
                }
            }

            int dwords = byte_count / 4;
            if (dwords > 0) {
                fprintf(output, "  mov ecx, %d\n", dwords);
                fprintf(output, "  cld\n");
                fprintf(output, "  rep movsd\n");
            }
            int remainder = byte_count % 4;
            if (remainder > 0) {
                fprintf(output, "  mov ecx, %d\n", remainder);
                fprintf(output, "  rep movsb\n");
            }
            break;
        }

        case IR_PHI:
            if (ir_temp->result) {
                if (is_temp(ir_temp->result)) {
                    temp_map_add(ir_temp->result, ir_temp->data_type);
                } else {
                    const char* base = strip_ssa_suffix(ir_temp->result);
                    if (get_const(base) == -1) {
                        Manual_add_const(ir_temp->data_type, "", 0, ir_temp->result,
                                         TYPE_VAR, ir_temp->data_type);
                    }
                }
            }
            break;

        case IR_STR_TO_STRING: {
            cache_flush_all(output);
            char* ptr_name = ir_temp->args1;

            if (is_temp(ptr_name)) {
                int off = temp_map_get_offset(ptr_name);
                fprintf(output, "  push dword [ebp-%d]\n", off - 4);
                fprintf(output, "  push dword [ebp-%d]\n", off);
            } else {
                cg_load(output, ptr_name, DATA_STR);
                fprintf(output, "  push edx\n");
                fprintf(output, "  push eax\n");
            }
            fprintf(output, "  call _str_to_string\n");
            fprintf(output, "  add esp, 8\n");
            cache_flush_all(output);

            cg_store(output, ir_temp->result, DATA_STRING, false);
            has_string_lib = true;
            temp_map_add(ir_temp->result, DATA_STRING);
            break;
        }

        case IR_TRUNCATE: {
            DataType dt = ir_temp->data_type;
            char* src = ir_temp->args1;
            DataType src_type = ir_temp->args1_type;
            cg_load(output, src, src_type);
            int mask;
            switch (dt) {
            case DATA_INT8:
            case DATA_UINT8:
            case DATA_CHAR:
            case DATA_BOOL:
                if (dt == DATA_INT8 || dt == DATA_CHAR) {
                    fprintf(output, "  shl eax, 24\n");
                    fprintf(output, "  sar eax, 24\n");
                } else {
                    fprintf(output, "  and eax, 0xFF\n");
                }
                break;
            case DATA_INT16:
            case DATA_UINT16:
                if (dt == DATA_INT16) {
                    fprintf(output, "  shl eax, 16\n");
                    fprintf(output, "  sar eax, 16\n");
                } else {
                    fprintf(output, "  and eax, 0xFFFF\n");
                }
                break;
            default:
                break;
            }
            cg_store(output, ir_temp->result, dt, false);
            break;
        }

        case IR_CAST: {
            DataType src = ir_temp->args1_type;
            DataType dst = ir_temp->data_type;
            DataType nsrc = normalize_type(src);
            DataType ndst = normalize_type(dst);

            bool dst_f = (ndst == DATA_FLOAT || ndst == DATA_UFLOAT ||
                          dst == DATA_FLOAT64 || dst == DATA_UFLOAT64);

            cg_load(output, ir_temp->args1, src);

            switch (ir_temp->cast_kind) {
            case CAST_INT_TO_FLOAT:
                fprintf(output, "  push eax\n");
                fprintf(output, "  fild dword [esp]\n");
                fprintf(output, "  add esp, 4\n");
                cg_store(output, ir_temp->result, dst, true);
                break;

            case CAST_FLOAT_TO_INT:
                if (get_type_size(dst) == 8) {
                    fprintf(output, "  sub esp, 8\n");
                    fprintf(output, "  fisttp qword [esp]\n");
                    cache_flush_reg(output, R_EDX);
                    fprintf(output, "  mov eax, dword [esp]\n");
                    fprintf(output, "  mov edx, dword [esp+4]\n");
                    fprintf(output, "  add esp, 8\n");
                } else {
                    fprintf(output, "  sub esp, 4\n");
                    fprintf(output, "  fisttp dword [esp]\n");
                    fprintf(output, "  mov eax, dword [esp]\n");
                    fprintf(output, "  add esp, 4\n");
                }
                cg_store(output, ir_temp->result, dst, false);
                break;

            case CAST_TRUNC:
                if (dst == DATA_BOOL) {
                    fprintf(output, "  test eax, eax\n");
                    fprintf(output, "  setne al\n");
                    fprintf(output, "  movzx eax, al\n");
                } else if (dst == DATA_CHAR) {
                    fprintf(output, "  movzx eax, al\n");
                } else if (dst == DATA_INT8 || dst == DATA_UINT8) {
                    fprintf(output, "  and eax, 0xFF\n");
                } else if (dst == DATA_INT16 || dst == DATA_UINT16) {
                    fprintf(output, "  and eax, 0xFFFF\n");
                }
                cg_store(output, ir_temp->result, dst, false);
                break;

            case CAST_SIGN_EXTEND: {
                int sb = get_type_size(src);
                int db = get_type_size(dst);
                if (sb == 1) fprintf(output, "  movsx eax, al\n");
                else if (sb == 2) fprintf(output, "  movsx eax, ax\n");
                else if (sb == 4 && db == 8) fprintf(output, "  cdq\n");
                cg_store(output, ir_temp->result, dst, false);
                break;
            }

            case CAST_ZERO_EXTEND: {
                int sb = get_type_size(src);
                int db = get_type_size(dst);
                if (sb == 1) fprintf(output, "  movzx eax, al\n");
                else if (sb == 2) fprintf(output, "  movzx eax, ax\n");
                else if (sb == 4 && db == 8) fprintf(output, "  xor edx, edx\n");
                cg_store(output, ir_temp->result, dst, false);
                break;
            }

            case CAST_DOWNCAST: {
                int target_id = ir_temp->class_id;
                int label_id = cg64_label_counter++;
                int isa_n = semantics_get_class_count();
                char* fail_msg = add_const(DATA_STRING,
                                           "runtime error: invalid cast",
                                           0, DATA_STRING);

                fprintf(output, "  test eax, eax\n");
                fprintf(output, "  jz .L_cast_fail_%d\n", label_id);
                fprintf(output, "  mov ecx, dword [eax]\n");
                fprintf(output, "  imul ecx, %d\n", isa_n);
                fprintf(output, "  add ecx, %d\n", target_id);
                fprintf(output, "  movzx edx, byte [_exc_class_isa + ecx]\n");
                fprintf(output, "  test edx, edx\n");
                fprintf(output, "  jz .L_cast_fail_%d\n", label_id);
                fprintf(output, "  jmp .L_cast_ok_%d\n", label_id);
                fprintf(output, ".L_cast_fail_%d:\n", label_id);
                fprintf(output, "  push %d\n", 3);
                fprintf(output, "  push %s\n", fail_msg);
                fprintf(output, "  call _print_err\n");
                fprintf(output, "  add esp, 8\n");
                fprintf(output, "  push 1\n");
                fprintf(output, "  call _system_exit\n");
                fprintf(output, ".L_cast_ok_%d:\n", label_id);

                cg_store(output, ir_temp->result, dst, false);
                has_print = true;
                has_system_lib = true;
                has_exception_lib = true;
                break;
            }

            case CAST_INT_TO_PTR:
            case CAST_PTR_TO_INT:
            case CAST_PTR_REINTERP:
            case CAST_NOP:
            default:
                cg_store(output, ir_temp->result, dst, dst_f);
                break;
        }

            if (ir_temp->result && is_temp(ir_temp->result)) {
                temp_map_add(ir_temp->result, dst);
            }
            break;
        }
        }
        ir_temp = ir_temp->next;
    }
}

void codegen_win32_end(FILE* output)
{
    fprintf(output,"section .text\n");
    fprintf(output,"global _main\n");
    fprintf(output,"extern _ExitProcess@4\n");
    fprintf(output, "extern _SetConsoleOutputCP@4\n");
    if (has_string_lib)
    {
        fprintf(output,"extern _str_length\n");
        fprintf(output,"extern _str_copy\n");
        fprintf(output,"extern _str_concat\n");
        fprintf(output,"extern _char_to_str\n");
        fprintf(output,"extern _char_concat\n");
        fprintf(output,"extern _char_pair_to_str\n");
        fprintf(output,"extern _str_plus_char\n");
        fprintf(output,"extern _char_plus_str\n");
        fprintf(output,"extern _str_equal\n");
        fprintf(output,"extern _str_not_equal\n");
        fprintf(output,"extern _str_char_at\n");
        fprintf(output,"extern _str_substring\n");
        fprintf(output,"extern _str_plus_char_2\n");
        fprintf(output,"extern _char_plus_str_2\n");
        fprintf(output,"extern _str_to_string\n");
        fprintf(output,"extern _str_substring_str\n");
        fprintf(output,"extern _str_concat_str\n");
        fprintf(output,"extern _str_to_string\n");
        fprintf(output,"extern _str_substring_str\n");
    }
    if (main_has_args)
    {
        fprintf(output, "extern _GetCommandLineArgc\n");
        fprintf(output, "extern _GetCommandLineArgv\n");
        has_memory_lib = true;
    }
    if (has_memory_lib)
    {
        fprintf(output,"extern _AllocMemory\n");
        fprintf(output,"extern _FreeMemory\n");
        has_print = true;
    }
    if (has_print)
    {
        fprintf(output,"extern _print\n");
        fprintf(output, "extern _set_float_mode\n");
        fprintf(output, "extern _set_float_precision\n");
        fprintf(output, "extern _print_err\n");
    }
    if (has_float_lib)
    {
        fprintf(output,"extern _fgt\n");
        fprintf(output,"extern _fge\n");
        fprintf(output,"extern _feq\n");
        fprintf(output,"extern _fne\n");
        fprintf(output,"extern _fle\n");
        fprintf(output,"extern _flt\n");
        fprintf(output,"extern _fmod_fpu\n");
        fprintf(output,"extern _float_to_bits\n");
        fprintf(output,"extern _bits_to_float\n");
    }
    if (has_bounds_check) {
        fprintf(output, "extern _array_bounds_error\n");
        fprintf(output, "extern _null_ptr_error\n");
    }
    if (has_exception_lib) {
        fprintf(output, "extern _error_raise\n");
        fprintf(output, "extern _exc_frame_push\n");
        fprintf(output, "extern _exc_frame_pop\n");
        fprintf(output, "extern _exc_get_current\n");
        fprintf(output, "extern _error_is_instance\n");
        fprintf(output, "extern _exc_setjmp\n");
        fprintf(output, "extern _exc_longjmp\n");
        fprintf(output, "extern _exc_current\n");
        fprintf(output, "extern _exc_class_parents\n");
        fprintf(output, "extern _exc_error_handlers\n");
    }
    if (has_file_lib) {
        for (int fi = 0; fi < file_extern_count; fi++) {
            fprintf(output, "extern %s\n", file_extern_names[fi]);
        }
    }
    if (has_socket_lib)
    {
        fprintf(output, "extern _socket_create\n");
        fprintf(output, "extern _socket_connect\n");
        fprintf(output, "extern _socket_bind\n");
        fprintf(output, "extern _socket_listen\n");
        fprintf(output, "extern _socket_accept\n");
        fprintf(output, "extern _socket_send\n");
        fprintf(output, "extern _socket_recv\n");
        fprintf(output, "extern _socket_close\n");
        fprintf(output, "extern _socket_last_error\n");
        fprintf(output, "extern _socket_sendto\n");
        fprintf(output, "extern _socket_recvfrom\n");
        fprintf(output, "extern _socket_sender_addr\n");
        fprintf(output, "extern _socket_sender_port\n");
        fprintf(output, "extern _socket_sender_ip\n");
        fprintf(output, "extern _socket_set_timeout\n");
        fprintf(output, "extern _socket_set_nonblock\n");
        fprintf(output, "extern _socket_set_reuseaddr\n");
        fprintf(output, "extern _socket_set_nodelay\n");
        fprintf(output, "extern _socket_get_error\n");
        fprintf(output, "extern _socket_peer_ip\n");
        fprintf(output, "extern _socket_peer_port\n");
        fprintf(output, "extern _socket_local_port\n");
        fprintf(output, "extern _socket_send_str\n");
        fprintf(output, "extern _socket_recv_str\n");
        fprintf(output, "extern _socket_send_all\n");
        fprintf(output, "extern _socket_recv_line\n");
        fprintf(output, "extern _socket_recv_exact\n");
        fprintf(output, "extern _socket_select_read\n");
        fprintf(output, "extern _socket_select_write\n");
        fprintf(output, "extern _socket_htonl\n");
        fprintf(output, "extern _socket_htons\n");
        fprintf(output, "extern _socket_ntohl\n");
        fprintf(output, "extern _socket_ntohs\n");
        fprintf(output, "extern _socket_inet_addr\n");
        fprintf(output, "extern _socket_inet_ntoa\n");
        fprintf(output, "extern _socket_error_string\n");
        fprintf(output, "extern _socket_bind_addr\n");
        fprintf(output, "extern _iocp_init\n");
        fprintf(output, "extern _iocp_start_worker\n");
        fprintf(output, "extern _socket_accept_async\n");
        fprintf(output, "extern _socket_recv_async\n");
        fprintf(output, "extern _socket_send_async\n");
        fprintf(output, "extern _ov_get_fd\n");
        fprintf(output, "extern _ov_get_buffer\n");
        fprintf(output, "extern _ov_get_capacity\n");
        fprintf(output, "extern _ov_get_user\n");
        fprintf(output, "extern _ov_set_user\n");
        fprintf(output, "extern _ov_set_callback\n");
        fprintf(output, "extern _iocp_get_handle\n");
        fprintf(output, "extern _iocp_associate\n");
    }
    if (has_window_lib)
    {
        fprintf(output, "extern Window_create_string_int_int\n");
        fprintf(output, "extern Window_close\n");
        fprintf(output, "extern Window_show\n");
        fprintf(output, "extern Window_hide\n");
        fprintf(output, "extern Window_set_title_string\n");
        fprintf(output, "extern Window_move_int_int\n");
        fprintf(output, "extern Window_resize_int_int\n");
        fprintf(output, "extern Window_get_width\n");
        fprintf(output, "extern Window_get_height\n");
        fprintf(output, "extern Window_update\n");
        fprintf(output, "extern Window_wait\n");
        fprintf(output, "extern Window_flush\n");
        fprintf(output, "extern Window_clear_int\n");
        fprintf(output, "extern Window_set_pixel_int_int_int\n");
        fprintf(output, "extern Window_draw_line_int_int_int_int_int\n");
        fprintf(output, "extern Window_draw_line_thick_int_int_int_int_int_int\n");
        fprintf(output, "extern Window_draw_rect_int_int_int_int_int\n");
        fprintf(output, "extern Window_fill_rect_int_int_int_int_int\n");
        fprintf(output, "extern Window_draw_round_rect_int_int_int_int_int_int\n");
        fprintf(output, "extern Window_fill_round_rect_int_int_int_int_int_int\n");
        fprintf(output, "extern Window_draw_circle_int_int_int_int\n");
        fprintf(output, "extern Window_fill_circle_int_int_int_int\n");
        fprintf(output, "extern Window_draw_triangle_int_int_int_int_int_int_int\n");
        fprintf(output, "extern Window_fill_triangle_int_int_int_int_int_int_int\n");
        fprintf(output, "extern Window_draw_text_int_int_string_int\n");
        fprintf(output, "extern Window_draw_text_center_int_string_int\n");
        fprintf(output, "extern Window_is_key_down_int\n");
        fprintf(output, "extern Window_is_key_pressed_int\n");
        fprintf(output, "extern Window_is_mouse_down_int\n");
        fprintf(output, "extern Window_is_mouse_clicked_int\n");
        fprintf(output, "extern Window_get_mouse_x\n");
        fprintf(output, "extern Window_get_mouse_y\n");
        fprintf(output, "extern Window_get_mouse_wheel\n");
        fprintf(output, "extern Window_set_font_size_int\n");
        fprintf(output, "extern Window_get_ticks\n");
        fprintf(output, "extern Window_sleep_int\n");
        fprintf(output, "extern Window_message_box_string_string\n");
        fprintf(output, "extern Window_draw_canvas_identifier_int_int\n");
        fprintf(output, "extern Window_draw_canvas_part_identifier_int_int_int_int_int_int\n");
        fprintf(output, "extern Window_draw_canvas_scaled_identifier_int_int_int_int\n");
        fprintf(output, "extern Window_draw_canvas_transparent_identifier_int_int\n");
        fprintf(output, "extern Window_draw_canvas_part_transparent_identifier_int_int_int_int_int_int\n");
        fprintf(output, "extern Canvas_load_string\n");
        fprintf(output, "extern Window_set_cursor_int\n");
        fprintf(output, "extern Window_get_char\n");
        fprintf(output, "extern Window_center\n");
        fprintf(output, "extern Window_draw_char_int_int_int_int\n");
        fprintf(output, "extern Window_start_timer_int\n");
        fprintf(output, "extern Window_stop_timer\n");
        fprintf(output, "extern Window_get_timer_hits\n");
        fprintf(output, "extern Window_is_valid\n");
        fprintf(output, "extern Window_is_minimized\n");
        fprintf(output, "extern Window_is_maximized\n");
        fprintf(output, "extern Window_is_visible\n");
        fprintf(output, "extern Window_set_focus\n");
        fprintf(output, "extern Window_has_focus\n");
        fprintf(output, "extern Window_set_topmost_int\n");
        fprintf(output, "extern Window_set_maximizable_int\n");
        fprintf(output, "extern Window_set_resizable_int\n");
        fprintf(output, "extern Window_minimize\n");
        fprintf(output, "extern Window_maximize\n");
        fprintf(output, "extern Window_restore\n");
        fprintf(output, "extern Window_set_close_blocked_int\n");
        fprintf(output, "extern Window_is_close_requested\n");
        fprintf(output, "extern Window_clear_close_request\n");
        fprintf(output, "extern Window_get_count\n");
        fprintf(output, "extern Window_get_at_int\n");
        fprintf(output, "extern Window_open_file_string\n");
        fprintf(output, "extern Window_save_file_string\n");
        fprintf(output, "extern Canvas_create_int_int\n");
        fprintf(output, "extern Canvas_destroy\n");
        fprintf(output, "extern Canvas_get_width\n");
        fprintf(output, "extern Canvas_get_height\n");
        fprintf(output, "extern Canvas_clear_int\n");
        fprintf(output, "extern Canvas_set_pixel_int_int_int\n");
        fprintf(output, "extern Canvas_fill_rect_int_int_int_int_int\n");
        fprintf(output, "extern Canvas_draw_rect_int_int_int_int_int\n");
        fprintf(output, "extern Canvas_draw_line_int_int_int_int_int\n");
        fprintf(output, "extern Canvas_fill_circle_int_int_int_int\n");
        fprintf(output, "extern Canvas_draw_circle_int_int_int_int\n");
        fprintf(output, "extern Canvas_draw_text_int_int_string_int\n");
        fprintf(output, "extern Canvas_get_pixel_int_int\n");
        fprintf(output, "extern Button_create_identifier_string_int_int_int_int\n");
        fprintf(output, "extern Button_set_text_string\n");
        fprintf(output, "extern Button_is_clicked\n");
        fprintf(output, "extern Button_destroy\n");
        fprintf(output, "extern TextBox_create_identifier_string_int_int_int_int\n");
        fprintf(output, "extern TextBox_set_text_string\n");
        fprintf(output, "extern TextBox_get_text\n");
        fprintf(output, "extern TextBox_clear\n");
        fprintf(output, "extern TextBox_destroy\n");
        fprintf(output, "extern Label_create_identifier_string_int_int_int_int\n");
        fprintf(output, "extern Label_set_text_string\n");
        fprintf(output, "extern Label_destroy\n");
        fprintf(output, "extern CheckBox_create_identifier_string_int_int_int_int\n");
        fprintf(output, "extern CheckBox_set_text_string\n");
        fprintf(output, "extern CheckBox_is_checked\n");
        fprintf(output, "extern CheckBox_set_checked_int\n");
        fprintf(output, "extern CheckBox_is_clicked\n");
        fprintf(output, "extern CheckBox_destroy\n");
        fprintf(output, "extern RadioButton_create_identifier_string_int_int_int_int\n");
        fprintf(output, "extern RadioButton_set_text_string\n");
        fprintf(output, "extern RadioButton_is_checked\n");
        fprintf(output, "extern RadioButton_set_checked_int\n");
        fprintf(output, "extern RadioButton_is_clicked\n");
        fprintf(output, "extern RadioButton_destroy\n");
        fprintf(output, "extern ComboBox_create_identifier_int_int_int_int\n");
        fprintf(output, "extern ComboBox_add_item_string\n");
        fprintf(output, "extern ComboBox_clear_items\n");
        fprintf(output, "extern ComboBox_get_selected\n");
        fprintf(output, "extern ComboBox_set_selected_int\n");
        fprintf(output, "extern ComboBox_get_selected_text\n");
        fprintf(output, "extern ComboBox_is_changed\n");
        fprintf(output, "extern ComboBox_destroy\n");
        fprintf(output, "extern ListBox_create_identifier_int_int_int_int\n");
        fprintf(output, "extern ListBox_add_item_string\n");
        fprintf(output, "extern ListBox_clear_items\n");
        fprintf(output, "extern ListBox_get_selected\n");
        fprintf(output, "extern ListBox_set_selected_int\n");
        fprintf(output, "extern ListBox_get_selected_text\n");
        fprintf(output, "extern ListBox_is_changed\n");
        fprintf(output, "extern ListBox_destroy\n");
        fprintf(output, "extern GroupBox_create_identifier_string_int_int_int_int\n");
        fprintf(output, "extern GroupBox_set_text_string\n");
        fprintf(output, "extern GroupBox_destroy\n");
        fprintf(output, "extern Window_layout_begin_int_int_int_int\n");
        fprintf(output, "extern Window_layout_end\n");
        fprintf(output, "extern Window_layout_space_int\n");
        fprintf(output, "extern Panel_create_identifier_int_int_int_int\n");
        fprintf(output, "extern Panel_set_background_int\n");
        fprintf(output, "extern Panel_destroy\n");
        fprintf(output, "extern Panel_move_int_int\n");
        fprintf(output, "extern Panel_set_size_int_int\n");
        fprintf(output, "extern Panel_show\n");
        fprintf(output, "extern Panel_hide\n");
        fprintf(output, "extern Button_move_int_int\n");
        fprintf(output, "extern Button_set_size_int_int\n");
        fprintf(output, "extern Button_show\n");
        fprintf(output, "extern Button_hide\n");
        fprintf(output, "extern TextBox_move_int_int\n");
        fprintf(output, "extern TextBox_set_size_int_int\n");
        fprintf(output, "extern TextBox_show\n");
        fprintf(output, "extern TextBox_hide\n");
        fprintf(output, "extern Label_move_int_int\n");
        fprintf(output, "extern Label_set_size_int_int\n");
        fprintf(output, "extern Label_show\n");
        fprintf(output, "extern Label_hide\n");
        fprintf(output, "extern CheckBox_move_int_int\n");
        fprintf(output, "extern CheckBox_set_size_int_int\n");
        fprintf(output, "extern CheckBox_show\n");
        fprintf(output, "extern CheckBox_hide\n");
        fprintf(output, "extern RadioButton_move_int_int\n");
        fprintf(output, "extern RadioButton_set_size_int_int\n");
        fprintf(output, "extern RadioButton_show\n");
        fprintf(output, "extern RadioButton_hide\n");
        fprintf(output, "extern ComboBox_move_int_int\n");
        fprintf(output, "extern ComboBox_set_size_int_int\n");
        fprintf(output, "extern ComboBox_show\n");
        fprintf(output, "extern ComboBox_hide\n");
        fprintf(output, "extern ListBox_move_int_int\n");
        fprintf(output, "extern ListBox_set_size_int_int\n");
        fprintf(output, "extern ListBox_show\n");
        fprintf(output, "extern ListBox_hide\n");
        fprintf(output, "extern GroupBox_move_int_int\n");
        fprintf(output, "extern GroupBox_set_size_int_int\n");
        fprintf(output, "extern GroupBox_show\n");
        fprintf(output, "extern GroupBox_hide\n");
        fprintf(output, "extern Menu_create_identifier\n");
        fprintf(output, "extern Menu_add_menu_string\n");
        fprintf(output, "extern Menu_add_item_string\n");
        fprintf(output, "extern Menu_add_separator\n");
        fprintf(output, "extern Menu_get_clicked\n");
        fprintf(output, "extern Menu_destroy\n");
        fprintf(output, "extern Toolbar_create_identifier_int\n");
        fprintf(output, "extern Toolbar_add_button_string\n");
        fprintf(output, "extern Toolbar_get_clicked\n");
        fprintf(output, "extern Toolbar_destroy\n");
        fprintf(output, "extern ButtonBar_create_identifier_int\n");
        fprintf(output, "extern ButtonBar_add_identifier_string\n");
        fprintf(output, "extern StatusBar_create_identifier\n");
        fprintf(output, "extern StatusBar_set_text_string\n");
        fprintf(output, "extern StatusBar_get_text\n");
        fprintf(output, "extern StatusBar_destroy\n");
        fprintf(output, "extern Menu_popup_int_int\n");
        fprintf(output, "extern ProgressBar_create_identifier_int_int_int_int\n");
        fprintf(output, "extern ProgressBar_set_range_int_int\n");
        fprintf(output, "extern ProgressBar_set_value_int\n");
        fprintf(output, "extern ProgressBar_get_value\n");
        fprintf(output, "extern ProgressBar_step_int\n");
        fprintf(output, "extern ProgressBar_set_bar_color_int\n");
        fprintf(output, "extern ProgressBar_set_marquee_int_int\n");
        fprintf(output, "extern ProgressBar_move_int_int\n");
        fprintf(output, "extern ProgressBar_set_size_int_int\n");
        fprintf(output, "extern ProgressBar_show\n");
        fprintf(output, "extern ProgressBar_hide\n");
        fprintf(output, "extern ProgressBar_destroy\n");
        fprintf(output, "extern SpinBox_create_identifier_int_int_int_int\n");
        fprintf(output, "extern SpinBox_set_range_int_int\n");
        fprintf(output, "extern SpinBox_set_step_int\n");
        fprintf(output, "extern SpinBox_set_value_int\n");
        fprintf(output, "extern SpinBox_get_value\n");
        fprintf(output, "extern SpinBox_is_changed\n");
        fprintf(output, "extern SpinBox_set_text_string\n");
        fprintf(output, "extern SpinBox_get_text\n");
        fprintf(output, "extern SpinBox_destroy\n");
        fprintf(output, "extern Slider_create_identifier_int_int_int_int\n");
        fprintf(output, "extern Slider_set_range_int_int\n");
        fprintf(output, "extern Slider_set_value_int\n");
        fprintf(output, "extern Slider_get_value\n");
        fprintf(output, "extern Slider_set_page_int\n");
        fprintf(output, "extern Slider_is_changed\n");
        fprintf(output, "extern Slider_move_int_int\n");
        fprintf(output, "extern Slider_set_size_int_int\n");
        fprintf(output, "extern Slider_show\n");
        fprintf(output, "extern Slider_hide\n");
        fprintf(output, "extern Slider_destroy\n");
        fprintf(output, "extern Window_popup_info_string_string\n");
        fprintf(output, "extern Window_popup_warning_string_string\n");
        fprintf(output, "extern Window_popup_error_string_string\n");
        fprintf(output, "extern Window_popup_question_string_string\n");
        fprintf(output, "extern Window_popup_yes_no_string_string\n");
        fprintf(output, "extern Window_popup_yes_no_cancel_string_string\n");
        fprintf(output, "extern Window_popup_ok_cancel_string_string\n");
        fprintf(output, "extern Window_popup_retry_cancel_string_string\n");
        fprintf(output, "extern Window_popup_input_string_string\n");
        fprintf(output, "extern Window_dc_fill_rect_int_int_int_int_int_int\n");
        fprintf(output, "extern Window_dc_draw_rect_int_int_int_int_int_int\n");
        fprintf(output, "extern Window_dc_set_pixel_int_int_int_int\n");
        fprintf(output, "extern Window_dc_draw_line_int_int_int_int_int_int\n");
        fprintf(output, "extern Window_dc_fill_circle_int_int_int_int_int\n");
        fprintf(output, "extern Window_dc_draw_circle_int_int_int_int_int\n");
        fprintf(output, "extern Window_dc_draw_text_int_int_int_string_int\n");
        fprintf(output, "extern Window_dc_get_text_width_int_string\n");
        fprintf(output, "extern Window_dc_fill_gradient_round_int_int_int_int_int_int_int_int\n");
        fprintf(output, "extern Window_draw_canvas_alpha_identifier_int_int\n");
        fprintf(output, "extern Window_draw_canvas_part_alpha_identifier_int_int_int_int_int_int\n");
        fprintf(output, "extern ComboBox_owner_draw\n");
        fprintf(output, "extern ComboBox_get_item_text_int\n");
        fprintf(output, "extern ListBox_get_item_text_int\n");
        fprintf(output, "extern Button_set_paint_int\n");
        fprintf(output, "extern Button_owner_draw\n");
        fprintf(output, "extern CheckBox_owner_draw\n");
        fprintf(output, "extern RadioButton_owner_draw\n");
        fprintf(output, "extern Label_owner_draw\n");
        fprintf(output, "extern Button_get_text\n");
        fprintf(output, "extern ProgressBar_owner_draw\n");
        fprintf(output, "extern Slider_owner_draw\n");
        fprintf(output, "extern SpinBox_owner_draw\n");
        fprintf(output, "extern StatusBar_owner_draw\n");
        fprintf(output, "extern GroupBox_owner_draw\n");
        fprintf(output, "extern Menu_owner_draw\n");
        fprintf(output, "extern Toolbar_owner_draw\n");
        fprintf(output, "extern Button_is_down\n");
        fprintf(output, "extern Button_is_hover\n");
        fprintf(output, "extern Button_has_focus\n");
        fprintf(output, "extern Button_refresh\n");
        fprintf(output, "extern CheckBox_set_paint_int\n");
        fprintf(output, "extern CheckBox_get_text\n");
        fprintf(output, "extern CheckBox_is_down\n");
        fprintf(output, "extern CheckBox_is_hover\n");
        fprintf(output, "extern CheckBox_has_focus\n");
        fprintf(output, "extern CheckBox_refresh\n");
        fprintf(output, "extern RadioButton_set_paint_int\n");
        fprintf(output, "extern RadioButton_get_text\n");
        fprintf(output, "extern RadioButton_is_down\n");
        fprintf(output, "extern RadioButton_is_hover\n");
        fprintf(output, "extern RadioButton_has_focus\n");
        fprintf(output, "extern RadioButton_refresh\n");
        fprintf(output, "extern Label_set_paint_int\n");
        fprintf(output, "extern Label_get_text\n");
        fprintf(output, "extern Label_refresh\n");
        fprintf(output, "extern TextBox_is_changed\n");
        fprintf(output, "extern TextBox_is_focused\n");
    }
    if (has_image_lib)
    {
        fprintf(output, "extern _image_load\n");
        fprintf(output, "extern _image_free\n");
    }
    if (has_music_lib)
    {
        fprintf(output, "extern Music_load_string\n");
        fprintf(output, "extern Music_play_int\n");
        fprintf(output, "extern Music_pause\n");
        fprintf(output, "extern Music_resume\n");
        fprintf(output, "extern Music_stop\n");
        fprintf(output, "extern Music_is_playing\n");
        fprintf(output, "extern Music_set_volume_int\n");
        fprintf(output, "extern Music_get_position\n");
        fprintf(output, "extern Music_unload\n");
    }
    if (has_video_lib)
    {
        fprintf(output, "extern Video_load_string\n");
        fprintf(output, "extern Video_unload\n");

        fprintf(output, "extern Video_next\n");
        fprintf(output, "extern Video_width\n");
        fprintf(output, "extern Video_height\n");
        fprintf(output, "extern Video_fps\n");
        fprintf(output, "extern Video_duration\n");
        fprintf(output, "extern Video_time\n");
        fprintf(output, "extern Video_seek_float\n");
        fprintf(output, "extern Video_rewind\n");
        fprintf(output, "extern Video_set_loop_int\n");
        fprintf(output, "extern Video_blit_to_identifier\n");
        fprintf(output, "extern Video_last_error\n");
        fprintf(output, "extern Video_dbg_subtype\n");
        fprintf(output, "extern Video_last_hresult\n");
        fprintf(output, "extern Video_play_int\n");
        fprintf(output, "extern Video_stop\n");
    }
    if (has_time_lib)
    {
        fprintf(output, "extern Time_now\n");
        fprintf(output, "extern Time_millis\n");
        fprintf(output, "extern Time_delay_int\n");
    }
    if (has_thread_lib)
    {
        fprintf(output, "extern _thread_create\n");
        fprintf(output, "extern _thread_join\n");
        fprintf(output, "extern _thread_self\n");
        fprintf(output, "extern _thread_sleep\n");
    }
    if (has_function_lib)
    {
        fprintf(output, "extern Function_call\n");
        fprintf(output, "extern Function_call_int\n");
        fprintf(output, "extern Function_raw\n");
        fprintf(output, "extern Function_isNull\n");
    }
    if (has_sync_lib)
    {
        fprintf(output, "extern _sync_mutex_create\n");
        fprintf(output, "extern _sync_mutex_lock\n");
        fprintf(output, "extern _sync_mutex_unlock\n");
        fprintf(output, "extern _sync_mutex_destroy\n");
        fprintf(output, "extern _sync_event_create\n");
        fprintf(output, "extern _sync_event_set\n");
        fprintf(output, "extern _sync_event_reset\n");
        fprintf(output, "extern _sync_event_wait\n");
        fprintf(output, "extern _sync_event_wait_timeout\n");
        fprintf(output, "extern _sync_event_destroy\n");
        fprintf(output, "extern _sync_semaphore_create\n");
        fprintf(output, "extern _sync_semaphore_acquire\n");
        fprintf(output, "extern _sync_semaphore_release\n");
        fprintf(output, "extern _sync_semaphore_destroy\n");
    }
    if (has_array_lib)
    {
        fprintf(output,"extern _array_to_string\n");
    }
    if (has_format)
    {
        fprintf(output, "extern _format_string\n");
    }
    if (has_input)
    {
        fprintf(output, "extern _input_parse\n");
    }

    if (has_system_lib || main_has_args) {
        fprintf(output, "extern _GetCommandLineArgc\n");
        fprintf(output, "extern _GetCommandLineArgv\n");
        fprintf(output, "extern _system_exit\n");
        fprintf(output, "extern _system_sleep\n");
        fprintf(output, "extern _system_time\n");
        fprintf(output, "extern _system_getenv\n");
        fprintf(output, "extern _system_call\n");
        fprintf(output, "extern _sys_argv_at\n");
    }

    if (has_64bit_ops)
    {
        fprintf(output, "extern _udiv64\n");
        fprintf(output, "extern _umod64\n");
        fprintf(output, "extern _div64\n");
        fprintf(output, "extern _mod64\n");
        fprintf(output, "extern _int64_to_str\n");
    }

    if (main_has_args) {
        fprintf(output, "_main:\n");
        fprintf(output, "  finit\n");
        fprintf(output, "  push ebp\n");
        fprintf(output, "  mov ebp, esp\n");
        fprintf(output, "  call _GetCommandLineArgc\n");
        fprintf(output, "  mov ebx, eax\n");
        fprintf(output, "  call _GetCommandLineArgv\n");
        fprintf(output, "  mov esi, eax\n");
        fprintf(output, "  mov eax, ebx\n");
        fprintf(output, "  shl eax, 2\n");
        fprintf(output, "  add eax, 4\n");
        fprintf(output, "  push ebx\n");
        fprintf(output, "  push eax\n");
        fprintf(output, "  call _gc_alloc\n");
        fprintf(output, "  add esp, 4\n");
        fprintf(output, "  pop ebx\n");
        fprintf(output, "  mov [eax], ebx\n");
        fprintf(output, "  add eax, 4\n");
        fprintf(output, "  xor ecx, ecx\n");
        fprintf(output, "_copy_argv:\n");
        fprintf(output, "  cmp ecx, ebx\n");
        fprintf(output, "  jge _call_usermain\n");
        fprintf(output, "  mov edx, [esi + ecx*4]\n");
        fprintf(output, "  mov [eax + ecx*4], edx\n");
        fprintf(output, "  inc ecx\n");
        fprintf(output, "  jmp _copy_argv\n");
        fprintf(output, "_call_usermain:\n");
        fprintf(output, "  push eax\n");
        fprintf(output, "  call _usermain\n");
        fprintf(output, "  add esp, 4\n");
        fprintf(output, "  push eax\n");
        fprintf(output, "  call _ExitProcess@4\n");

        has_memory_lib = true;
        has_gc = true;
    }

    if (has_gc) {
        fprintf(output, "extern _gc_init\n");
        fprintf(output, "extern _gc_alloc\n");
        fprintf(output, "extern _gc_alloc_ptr_array\n");
        fprintf(output, "extern _gc_collect\n");
        fprintf(output, "extern _gc_force\n");
        fprintf(output, "extern _gc_status\n");
        fprintf(output, "global gc_data_start_label\n");
        fprintf(output, "global gc_data_end_label\n");
        fprintf(output, "global gc_bss_start_label\n");
        fprintf(output, "global gc_bss_end_label\n");
        fprintf(output, "global gc_static_data_start_label\n");
        fprintf(output, "global gc_static_data_end_label\n");
        fprintf(output, "global gc_static_bss_start_label\n");
        fprintf(output, "global gc_static_bss_end_label\n");
    }
    if (has_sqlite) {
        fprintf(output, "extern _sqlite_open\n");
        fprintf(output, "extern _sqlite_close\n");
        fprintf(output, "extern _sqlite_exec\n");
        fprintf(output, "extern _sqlite_prepare\n");
        fprintf(output, "extern _sqlite_step\n");
        fprintf(output, "extern _sqlite_finalize\n");
        fprintf(output, "extern _sqlite_reset\n");
        fprintf(output, "extern _sqlite_bind_int\n");
        fprintf(output, "extern _sqlite_bind_int64\n");
        fprintf(output, "extern _sqlite_bind_text\n");
        fprintf(output, "extern _sqlite_bind_null\n");
        fprintf(output, "extern _sqlite_column_int\n");
        fprintf(output, "extern _sqlite_column_int64\n");
        fprintf(output, "extern _sqlite_column_text\n");
        fprintf(output, "extern _sqlite_column_bytes\n");
        fprintf(output, "extern _sqlite_column_type\n");
        fprintf(output, "extern _sqlite_last_insert_rowid\n");
        fprintf(output, "extern _sqlite_changes\n");
        fprintf(output, "extern _sqlite_errcode\n");
        fprintf(output, "extern _sqlite_errmsg\n");
    }
    if (has_unsafe_lib) {
        fprintf(output, "extern _unsafe_alloc\n");
        fprintf(output, "extern _unsafe_address_alloc\n");
        fprintf(output, "extern _unsafe_free\n");
    }
    fprintf(output, "section .static_data data\n");
    if (has_gc) {
        fprintf(output, "  gc_static_data_start_label:\n");
    }
    for (int i = 0; i < consts->index; i++) {
        if (consts->storage[i] == STORAGE_STATIC_LOCAL &&
            consts->var_type[i] != TYPE_UNINIT_VAR) {
            switch (consts->type[i]) {
                case DATA_INT:
                    fprintf(output, "  %s dd %s\n", consts->name[i],
                            consts->value[i] ? consts->value[i] : "0");
                    break;
                case DATA_FLOAT: case DATA_UFLOAT:
                    fprintf(output, "  %s dd %s\n", consts->name[i],
                            consts->value[i] ? consts->value[i] : "0.0");
                    break;
                default:
                    fprintf(output, "  %s dd 0\n", consts->name[i]);
                    break;
            }
        }
    }
    for (int i = 0; i < consts->index; i++) {
        if (consts->storage[i] == STORAGE_STATIC_GUARD) {
            fprintf(output, "  %s dd 0\n", consts->name[i]);
        }
    }
    if (has_gc) {
        fprintf(output, "  gc_static_data_end_label:\n");
    }
    fprintf(output, "section .static_bss bss\n");
    if (has_gc) {
        fprintf(output, "  gc_static_bss_start_label:\n");
    }
    for (int i = 0; i < consts->index; i++) {
        if (consts->storage[i] == STORAGE_STATIC_LOCAL &&
            consts->var_type[i] == TYPE_UNINIT_VAR) {
            switch (consts->type[i]) {
                case DATA_INT: case DATA_UINT:
                    fprintf(output, "  %s resd 1\n", consts->name[i]); break;
                default:
                    fprintf(output, "  %s resd 1\n", consts->name[i]); break;
            }
        }
    }
    if (has_gc) {
        fprintf(output, "  gc_static_bss_end_label:\n");
    }
    fprintf(output,"section .data\n");
    if (has_gc) {
        fprintf(output, "  gc_data_start_label:\n");
    }
    if (has_exception_lib) {
        fprintf(output, "  _exc_current dd 0\n");
    }
    for (int i = 0;i < consts->index;i++)
    {
        if (consts->var_type[i] == TYPE_VAR &&
            consts->storage[i] == STORAGE_GLOBAL && !consts->is_array[i])
        {
            if (consts->is_dynamic_array[i]) {
                fprintf(output, "  %s dd 0, 0\n", consts->name[i]);
                continue;
            }
            switch (consts->type[i])
            {
            case DATA_INT:
            case DATA_UINT:
                if (is_var_name(consts->value[i]))
                {
                    fprintf(output,"  %s dd 0\n",consts->name[i]);
                } else
                {
                    fprintf(output,"  %s dd %s\n",consts->name[i],consts->value[i]);
                }
                break;
            case DATA_INT64:
            case DATA_UINT64:
                fprintf(output,"  %s dq %s\n", consts->name[i], consts->value[i]);
                break;
            case DATA_FLOAT:
            case DATA_UFLOAT:
                if (is_var_name(consts->value[i]))
                {
                    fprintf(output,"  %s dd 0.0\n",consts->name[i]);
                } else
                {
                    const char* val = consts->value[i];
                    if (val && val[0] == '.') {
                        fprintf(output,"  %s dd 0%s\n", consts->name[i], val);
                    } else if (val && val[strlen(val)-1] == '.') {
                        fprintf(output,"  %s dd %s0\n", consts->name[i], val);
                    } else {
                        fprintf(output,"  %s dd %s\n", consts->name[i], val);
                    }
                }
                break;
            case DATA_BOOL:
                if (is_var_name(consts->value[i]))
                {
                    fprintf(output,"  %s db 0,0\n",consts->name[i]);
                } else
                {
                    fprintf(output,"  %s db %s,0\n",consts->name[i],consts->value[i]);
                }
                break;
            case DATA_CHAR:
                if (is_var_name(consts->value[i]) && consts->value_type[i] != DATA_CHAR)
                {
                    fprintf(output,"  %s db ` `,0\n",consts->name[i]);
                } else
                {
                    char* esc = escape_for_nasm(consts->value[i]);
                    fprintf(output,"  %s db `%s`,0\n",consts->name[i], esc);
                    free(esc);
                }
                break;
            case DATA_STRING:
                if (is_var_name(consts->value[i]) && consts->value_type[i] != DATA_STRING)
                {
                    fprintf(output,"  %s db ` `,0\n",consts->name[i]);
                } else if (consts->value[i] != NULL
                           && consts->value[i][0] == '_'
                           && consts->value[i][1] == 's'
                           && consts->value[i][2] == 't'
                           && consts->value[i][3] == 'r') {
                    int is_str_label = 1;
                    for (int k = 4; consts->value[i][k]; k++) {
                        if (consts->value[i][k] < '0' || consts->value[i][k] > '9') {
                            is_str_label = 0;
                            break;
                        }
                    }
                    if (is_str_label && consts->value[i][4] != '\0') {
                        fprintf(output, "  %s dd %s\n", consts->name[i], consts->value[i]);
                    } else {
                        char* esc = escape_for_nasm(consts->value[i]);
                        fprintf(output,"  %s db `%s`,0\n",consts->name[i], esc);
                        free(esc);
                    }
                           } else
                           {
                               char* esc = escape_for_nasm(consts->value[i]);
                               fprintf(output,"  %s db `%s`,0\n",consts->name[i], esc);
                               free(esc);
                           }
                break;
            case DATA_IDENTIFIER:
                fprintf(output,"  %s dd 0\n",consts->name[i]);
                break;
            case DATA_STR:
                fprintf(output,"  %s dd 0, 0\n",consts->name[i]);
                break;
            }
        }
    }

    for (GlobalArrayInit* gai = global_array_inits; gai; gai = gai->next) {
        fprintf(output, "  %s: dd ", gai->name);
        for (int j = 0; j < gai->count; j++) {
            if (j > 0) fprintf(output, ", ");
            fprintf(output, "%d", gai->values[j]);
        }
        fprintf(output, "\n");
    }

    if (has_gc) {
        fprintf(output, "  gc_data_end_label:\n");
    }

    fprintf(output,"section .bss\n");
    if (has_gc) {
        fprintf(output, "  gc_bss_start_label:\n");
    }
    for (int i = 0;i < consts->index;i++)
    {
        if (consts->var_type[i] == TYPE_UNINIT_VAR && consts->storage[i] == STORAGE_GLOBAL)
        {
            if (consts->is_dynamic_array[i]) {
                fprintf(output, "  %s resd 2\n", consts->name[i]);
                continue;
            }
            if (consts->is_array[i]) {
                int total = consts->array_size[i];
                fprintf(output,"  %s resd %d\n", consts->name[i], total);
                continue;
            }
            switch (consts->type[i])
            {
            case DATA_INT:
            case DATA_UINT:
                fprintf(output,"  %s resd 1\n",consts->name[i]);
                break;
            case DATA_FLOAT:
            case DATA_UFLOAT:
                fprintf(output,"  %s resd 2\n",consts->name[i]);
                break;
            case DATA_BOOL:
                fprintf(output,"  %s resb 4\n",consts->name[i]);
                break;
            case DATA_CHAR:
                fprintf(output,"  %s resb 1\n",consts->name[i]);
                break;
            case DATA_STRING:
                fprintf(output,"  %s resb 4\n",consts->name[i]);
                break;
            case DATA_IDENTIFIER:
                fprintf(output,"  %s resd 1\n",consts->name[i]);
                break;
            case DATA_STR:
                fprintf(output,"  %s resd 2\n",consts->name[i]);
                break;
            }
        }
    }

    if (has_gc) {
        fprintf(output, "  gc_bss_end_label:\n");
    }

    fprintf(output,"section .rodata\n");
    fprintf(output,"global rodata_start_label\n");
    fprintf(output,"global rodata_end_label\n");
    fprintf(output,"rodata_start_label:\n");
    ClassEntry* vcls = semantics_get_all_classes();
    while (vcls) {
        if (vcls->vtable_size > 0) {
            char* vlbl = class_vtable_label(vcls->name);
            fprintf(output, "%s:\n", vlbl);
            for (int vi = 0; vi < vcls->vtable_size; vi++) {
                const char* fn = resolve_vtable_slot(vcls, vi);
                fprintf(output, "  dd %s\n", fn ? fn : "0");
            }
            free(vlbl);
        }
        vcls = vcls->next;
    }
    if (has_exception_lib) {
        ClassIdEntry* ids = semantics_get_class_ids();
        int max_id = 0;
        for (ClassIdEntry* e = ids; e; e = e->next)
            if (e->class_id > max_id) max_id = e->class_id;
        fprintf(output, "_exc_class_parents:\n");
        for (int i = 0; i <= max_id; i++) {
            int parent = 0;
            for (ClassIdEntry* e = ids; e; e = e->next)
                if (e->class_id == i) { parent = e->parent_id; break; }
            fprintf(output, "  dd %d\n", parent);
        }

        int isa_n = 0;
        unsigned char* isa_tbl = NULL;
        semantics_get_class_isa(&isa_n, &isa_tbl);
        if (isa_tbl && isa_n > 0) {
            fprintf(output, "_exc_class_isa:\n");
            for (int i = 0; i < isa_n; i++) {
                fprintf(output, "  db ");
                for (int j = 0; j < isa_n; j++) {
                    if (j > 0) fprintf(output, ", ");
                    fprintf(output, "%d", isa_tbl[(size_t)i * isa_n + j]);
                }
                fprintf(output, "\n");
            }
            free(isa_tbl);
        }

        fprintf(output, "_exc_error_handlers:\n");
        for (int i = 0; i <= max_id; i++) {
            const char* handler = "0";
            for (ClassIdEntry* e = ids; e; e = e->next)
                if (e->class_id == i && e->error_handler) { handler = e->error_handler; break; }
            fprintf(output, "  dd %s\n", handler);
        }
        while (ids) {
            ClassIdEntry* n = ids->next;
            free(ids->error_handler);
            free(ids);
            ids = n;
        }
    }
    for (int i = 0;i < consts->index;i++)
    {
        if (consts->var_type[i] == TYPE_CONST)
        {
            switch (consts->type[i])
            {
            case DATA_INT:
            case DATA_UINT:
                if (is_var_name(consts->value[i]))
                {
                    fprintf(output,"  %s dd 0\n",consts->name[i]);
                } else
                {
                    fprintf(output,"  %s dd %s\n",consts->name[i],consts->value[i]);
                }
                break;
            case DATA_INT64:
            case DATA_UINT64:
                fprintf(output,"  %s dq %s\n", consts->name[i], consts->value[i]);
                break;
            case DATA_FLOAT:
            case DATA_UFLOAT:
                if (is_var_name(consts->value[i]))
                {
                    fprintf(output,"  %s dd 0.0\n",consts->name[i]);
                } else
                {
                    const char* val = consts->value[i];
                    if (val && val[0] == '.') {
                        fprintf(output,"  %s dd 0%s\n", consts->name[i], val);
                    } else if (val && val[strlen(val)-1] == '.') {
                        fprintf(output,"  %s dd %s0\n", consts->name[i], val);
                    } else {
                        fprintf(output,"  %s dd %s\n", consts->name[i], val);
                    }
                }
                break;
            case DATA_BOOL:
                if (is_var_name(consts->value[i]))
                {
                    fprintf(output,"  %s db `false`,0\n",consts->name[i]);
                } else
                {
                    char* esc = escape_for_nasm(consts->value[i]);
                    fprintf(output,"  %s db `%s`,0\n",consts->name[i], esc);
                    free(esc);
                }
                break;
            case DATA_CHAR:
                if (is_var_name(consts->value[i]) && consts->value_type[i] != DATA_CHAR)
                {
                    fprintf(output,"  %s db ` `,0\n",consts->name[i]);
                } else
                {
                    char* esc = escape_for_nasm(consts->value[i]);
                    fprintf(output,"  %s db `%s`,0\n",consts->name[i], esc);
                    free(esc);
                }
                break;
            case DATA_STRING:
                if (is_var_name(consts->value[i]) && consts->value_type[i] != DATA_STRING)
                {
                    fprintf(output,"  %s db ` `,0\n",consts->name[i]);
                } else
                {
                    char* esc = escape_for_nasm(consts->value[i]);
                    fprintf(output,"  %s db `%s`,0\n",consts->name[i], esc);
                    free(esc);
                }
                break;
            case DATA_IDENTIFIER:
                fprintf(output,"  %s dd 0\n",consts->name[i]);
                break;
            }
        }
    }
    fprintf(output,"rodata_end_label:\n");
}

void cleanup_consts()
{
    if (consts != NULL) {
        for (int i = 0; i < consts->index; i++) {
            free(consts->value[i]);
            free(consts->name[i]);
        }
        free(consts->type);
        free(consts->value);
        free(consts->name);
        free(consts->var_type);
        free(consts->value_type);
        free(consts->storage);
        free(consts->offset);
        free(consts->is_array);
        free(consts->array_size);
        free(consts->is_dynamic_array);
        free(consts);
        consts = NULL;
    }
    temp_map_cleanup();
}

void codegen_cleanup(void)
{
    cleanup_consts();
    temp_map_cleanup();
    for (int fi = 0; fi < file_extern_count; fi++) {
        free(file_extern_names[fi]);
    }
    file_extern_count = 0;
    escape_set_reset();
    free(escaped_vars);
    escaped_vars = NULL;
    escaped_var_capacity = 0;
    for (int i = 0; i < 5; i++) {
        free(cache_slots[i].var_name);
        cache_slots[i].var_name = NULL;
        cache_slots[i].in_use = false;
        cache_slots[i].dirty = false;
        cache_slots[i].has_addr = false;
    }
    while (global_array_inits) {
        GlobalArrayInit* next = global_array_inits->next;
        free(global_array_inits->name);
        free(global_array_inits->values);
        free(global_array_inits);
        global_array_inits = next;
    }
}
