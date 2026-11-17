#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include "YString.h"

/**
 * @brief Create a String object from a C-style string.
 * 从C字符串创建String对象。
 * @param cstr The null-terminated source string. / 以空字符结尾的源字符串。
 * @return Pointer to the new String, or NULL on failure. / 返回新String的指针，失败返回NULL。
 */
String* string_create(const char* cstr) {
    if (!cstr) return NULL;

    String* str = (String*)malloc(sizeof(String));
    if (!str) return NULL;

    str->length = strlen(cstr);
    str->capacity = str->length + 1;
    str->data = (char*)malloc(str->capacity);

    if (!str->data) {
        free(str);
        return NULL;
    }

    strcpy(str->data, cstr);
    return str;
}

/**
 * @brief Create an empty String with initial capacity.
 * 创建一个具有初始容量的空字符串。
 * @return Pointer to the new empty String, or NULL on failure. / 返回新的空String指针，失败返回NULL。
 */
String* string_create_empty() {
    String* str = (String*)malloc(sizeof(String));
    if (!str) return NULL;

    str->length = 0;
    str->capacity = 16;
    str->data = (char*)malloc(str->capacity);

    if (!str->data) {
        free(str);
        return NULL;
    }

    str->data[0] = '\0';
    return str;
}

/**
 * @brief Create a String representation of an integer.
 * 从整数创建字符串表示。
 * @param value The integer value to convert. / 待转换的整数值。
 * @return Pointer to the new String, or NULL on failure. / 返回新String指针，失败返回NULL。
 */
String* string_create_from_int(int value) {
    String* str = string_create_empty();
    if (!str) return NULL;

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    string_set(str, buffer);

    return str;
}

/**
 * @brief Create a String representation of a double with given precision.
 * 以指定精度从双精度浮点数创建字符串表示。
 * @param value The double value to convert. / 待转换的双精度值。
 * @param precision Number of digits after the decimal point. / 小数点后的位数。
 * @return Pointer to the new String, or NULL on failure. / 返回新String指针，失败返回NULL。
 */
String* string_create_from_double(double value, int precision) {
    String* str = string_create_empty();
    if (!str) return NULL;

    char format[16];
    snprintf(format, sizeof(format), "%%.%df", precision);

    char buffer[64];
    snprintf(buffer, sizeof(buffer), format, value);
    string_set(str, buffer);

    return str;
}

/**
 * @brief Create a deep copy of a String.
 * 深拷贝一个字符串。
 * @param str Source String to clone. / 要克隆的源字符串。
 * @return Pointer to the new String copy, or NULL if source is NULL. / 返回新副本的指针，若源为NULL则返回NULL。
 */
String* string_clone(const String* str) {
    if (!str) return NULL;
    return string_create(str->data);
}

/**
 * @brief Destroy a String and free its memory.
 * 销毁String并释放其内存。
 * @param str Pointer to the String to destroy. / 指向要销毁的字符串的指针。
 */
void string_destroy(String* str) {
    if (str) {
        free(str->data);
        free(str);
    }
}

/**
 * @brief Ensure the String's internal buffer has at least min_capacity bytes.
 * 确保字符串内部缓冲区至少有 min_capacity 字节的空间。
 * @param str Target string. / 目标字符串。
 * @param min_capacity Minimum required capacity (including null terminator). / 所需的最小容量（含终止符）。
 * @return 1 on success, 0 on memory allocation failure. / 成功返回1，内存分配失败返回0。
 */
int string_ensure_capacity(String* str, size_t min_capacity) {
    if (str->capacity >= min_capacity) return 1;

    size_t new_capacity = str->capacity * 2;
    if (new_capacity < min_capacity) new_capacity = min_capacity;

    char* new_data = (char*)realloc(str->data, new_capacity);
    if (!new_data) return 0;

    str->data = new_data;
    str->capacity = new_capacity;
    return 1;
}

/**
 * @brief Shrink the internal buffer to exactly fit the current content.
 * 收缩内部缓冲区以恰好容纳当前内容。
 * @param str Target string. / 目标字符串。
 */
void string_shrink_to_fit(String* str) {
    if (str->capacity > str->length + 1) {
        char* new_data = (char*)realloc(str->data, str->length + 1);
        if (new_data) {
            str->data = new_data;
            str->capacity = str->length + 1;
        }
    }
}

/**
 * @brief Get a read-only C-string pointer.
 * 获取只读的C字符串指针。
 * @param str Source string. / 源字符串。
 * @return Pointer to the internal character array, or NULL if str is NULL. / 返回内部字符数组指针，若str为NULL则返回NULL。
 */
const char* string_cstr(const String* str) {
    return str ? str->data : NULL;
}

/**
 * @brief Get the length of the string (excluding null terminator).
 * 获取字符串长度（不含终止符）。
 * @param str Source string. / 源字符串。
 * @return Number of characters, or 0 if str is NULL. / 返回字符数，若str为NULL则返回0。
 */
size_t string_length(const String* str) {
    return str ? str->length : 0;
}

/**
 * @brief Check if a string is empty.
 * 检查字符串是否为空。
 * @param str Source string. / 源字符串。
 * @return 1 if string is NULL or has zero length, 0 otherwise. / 若字符串为NULL或长度为0返回1，否则返回0。
 */
int string_is_empty(const String* str) {
    return !str || str->length == 0;
}

/**
 * @brief Compare two String objects for equality.
 * 比较两个String对象是否相等。
 * @param str1 First string. / 第一个字符串。
 * @param str2 Second string. / 第二个字符串。
 * @return 1 if equal, 0 otherwise. / 相等返回1，否则返回0。
 */
int string_equals(const String* str1, const String* str2) {
    if (!str1 || !str2) return str1 == str2;
    if (str1->length != str2->length) return 0;
    return strcmp(str1->data, str2->data) == 0;
}

/**
 * @brief Compare a String with a C-string for equality.
 * 比较String与C字符串是否相等。
 * @param str A String object. / String对象。
 * @param cstr A null-terminated C-string. / 以空字符结尾的C字符串。
 * @return 1 if equal, 0 otherwise. / 相等返回1，否则返回0。
 */
int string_equals_cstr(const String* str, const char* cstr) {
    if (!str || !cstr) return str == NULL && cstr == NULL;
    return strcmp(str->data, cstr) == 0;
}

/**
 * @brief Case-insensitive comparison of two String objects.
 * 不区分大小写比较两个String对象。
 * @param str1 First string. / 第一个字符串。
 * @param str2 Second string. / 第二个字符串。
 * @return 1 if equal ignoring case, 0 otherwise. / 忽略大小写相等返回1，否则返回0。
 */
int string_equals_ignore_case(const String* str1, const String* str2) {
    if (!str1 || !str2) return str1 == str2;
    if (str1->length != str2->length) return 0;

    for (size_t i = 0; i < str1->length; i++) {
        if (tolower(str1->data[i]) != tolower(str2->data[i])) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Lexicographically compare two String objects.
 * 按字典序比较两个String对象。
 * @param str1 First string. / 第一个字符串。
 * @param str2 Second string. / 第二个字符串。
 * @return Negative if str1 < str2, positive if str1 > str2, 0 if equal.
 *         若 str1 < str2 返回负数，若 str1 > str2 返回正数，相等返回0。
 */
int string_compare(const String* str1, const String* str2) {
    if (!str1 || !str2) {
        if (!str1 && !str2) return 0;
        return str1 ? 1 : -1;
    }
    return strcmp(str1->data, str2->data);
}

/**
 * @brief Check if a string starts with a given prefix.
 * 检查字符串是否以指定前缀开头。
 * @param str The string to examine. / 待检查的字符串。
 * @param prefix The prefix to look for. / 要匹配的前缀。
 * @return 1 if str starts with prefix, 0 otherwise. / 若以前缀开头返回1，否则返回0。
 */
int string_starts_with(const String* str, const String* prefix) {
    if (!str || !prefix) return 0;
    if (prefix->length > str->length) return 0;

    return strncmp(str->data, prefix->data, prefix->length) == 0;
}

/**
 * @brief Check if a string ends with a given suffix.
 * 检查字符串是否以指定后缀结尾。
 * @param str The string to examine. / 待检查的字符串。
 * @param suffix The suffix to look for. / 要匹配的后缀。
 * @return 1 if str ends with suffix, 0 otherwise. / 若以后缀结尾返回1，否则返回0。
 */
int string_ends_with(const String* str, const String* suffix) {
    if (!str || !suffix) return 0;
    if (suffix->length > str->length) return 0;

    return strncmp(str->data + str->length - suffix->length,
                  suffix->data, suffix->length) == 0;
}

/**
 * @brief Append another String to the end of a String.
 * 将另一个字符串追加到目标字符串末尾。
 * @param str Target string to modify. / 要修改的目标字符串。
 * @param other String to append. / 要追加的字符串。
 */
void string_append(String* str, const String* other) {
    if (!str || !other) return;

    size_t new_length = str->length + other->length;
    if (!string_ensure_capacity(str, new_length + 1)) return;

    strcat(str->data, other->data);
    str->length = new_length;
}

/**
 * @brief Append a C-string to the end of a String.
 * 将C字符串追加到目标字符串末尾。
 * @param str Target string to modify. / 要修改的目标字符串。
 * @param cstr Null-terminated C-string to append. / 要追加的C字符串。
 */
void string_append_cstr(String* str, const char* cstr) {
    if (!str || !cstr) return;

    size_t cstr_len = strlen(cstr);
    size_t new_length = str->length + cstr_len;
    if (!string_ensure_capacity(str, new_length + 1)) return;

    strcat(str->data, cstr);
    str->length = new_length;
}

/**
 * @brief Append a single character to the end of a String.
 * 在字符串末尾追加一个字符。
 * @param str Target string to modify. / 要修改的目标字符串。
 * @param c The character to append. / 要追加的字符。
 */
void string_append_char(String* str, char c) {
    if (!str) return;

    size_t new_length = str->length + 1;
    if (!string_ensure_capacity(str, new_length + 1)) return;

    str->data[str->length] = c;
    str->data[new_length] = '\0';
    str->length = new_length;
}

/**
 * @brief Append the decimal representation of an integer to a String.
 * 将整数的十进制表示追加到字符串末尾。
 * @param str Target string to modify. / 要修改的目标字符串。
 * @param value The integer to append. / 要追加的整数。
 */
void string_append_int(String* str, int value) {
    if (!str) return;

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    string_append_cstr(str, buffer);
}

/**
 * @brief Append a double with specified precision to a String.
 * 将指定精度的双精度浮点数追加到字符串末尾。
 * @param str Target string to modify. / 要修改的目标字符串。
 * @param value The double value to append. / 要追加的双精度值。
 * @param precision Number of fractional digits. / 小数位数。
 */
void string_append_double(String* str, double value, int precision) {
    if (!str) return;

    char format[16];
    snprintf(format, sizeof(format), "%%.%df", precision);

    char buffer[64];
    snprintf(buffer, sizeof(buffer), format, value);
    string_append_cstr(str, buffer);
}

/**
 * @brief Insert another String at a given index.
 * 在指定位置插入另一个字符串。
 * @param str Target string to modify. / 要修改的目标字符串。
 * @param index Position (0-based) where to insert. / 插入位置（从0开始）。
 * @param other String to insert. / 要插入的字符串。
 */
void string_insert(String* str, size_t index, const String* other) {
    if (!str || !other || index > str->length) return;

    size_t new_length = str->length + other->length;
    if (!string_ensure_capacity(str, new_length + 1)) return;

    memmove(str->data + index + other->length,
            str->data + index,
            str->length - index + 1);

    memcpy(str->data + index, other->data, other->length);
    str->length = new_length;
}

/**
 * @brief Insert a C-string at a given index.
 * 在指定位置插入一个C字符串。
 * @param str Target string to modify. / 要修改的目标字符串。
 * @param index Position (0-based) where to insert. / 插入位置（从0开始）。
 * @param cstr Null-terminated C-string to insert. / 要插入的C字符串。
 */
void string_insert_cstr(String* str, size_t index, const char* cstr) {
    if (!str || !cstr || index > str->length) return;

    size_t cstr_len = strlen(cstr);
    size_t new_length = str->length + cstr_len;
    if (!string_ensure_capacity(str, new_length + 1)) return;

    memmove(str->data + index + cstr_len,
            str->data + index,
            str->length - index + 1);

    memcpy(str->data + index, cstr, cstr_len);
    str->length = new_length;
}

/**
 * @brief Remove a range of characters from a String.
 * 从字符串中删除一段字符。
 * @param str Target string to modify. / 要修改的目标字符串。
 * @param index Starting position to remove. / 删除的起始位置。
 * @param count Number of characters to remove. / 要删除的字符数。
 */
void string_remove(String* str, size_t index, size_t count) {
    if (!str || index >= str->length) return;

    if (index + count > str->length) {
        count = str->length - index;
    }

    memmove(str->data + index,
            str->data + index + count,
            str->length - index - count + 1);

    str->length -= count;
}

/**
 * @brief Clear the content of a String, making it an empty string.
 * 清空字符串内容，使其成为空字符串。
 * @param str Target string to clear. / 要清空的目标字符串。
 */
void string_clear(String* str) {
    if (!str) return;

    str->data[0] = '\0';
    str->length = 0;
}

/**
 * @brief Set the content of a String to a given C-string.
 * 将字符串内容设置为给定的C字符串。
 * @param str Target string to modify. / 要修改的目标字符串。
 * @param cstr Source null-terminated string. / 源C字符串。
 */
void string_set(String* str, const char* cstr) {
    if (!str || !cstr) return;

    size_t new_length = strlen(cstr);
    if (!string_ensure_capacity(str, new_length + 1)) return;

    strcpy(str->data, cstr);
    str->length = new_length;
}

/**
 * @brief Copy the content of another String into the destination.
 * 将另一个String的内容复制到目标字符串。
 * @param dest Destination string. / 目标字符串。
 * @param src Source String. / 源字符串。
 */
void string_set_string(String* dest, const String* src) {
    if (!dest || !src) return;
    string_set(dest, src->data);
}

/**
 * @brief Convert all characters in a String to uppercase.
 * 将字符串中的所有字符转换为大写。
 * @param str Target string to modify. / 要修改的字符串。
 */
void string_to_upper(String* str) {
    if (!str) return;

    for (size_t i = 0; i < str->length; i++) {
        str->data[i] = toupper(str->data[i]);
    }
}

/**
 * @brief Convert all characters in a String to lowercase.
 * 将字符串中的所有字符转换为小写。
 * @param str Target string to modify. / 要修改的字符串。
 */
void string_to_lower(String* str) {
    if (!str) return;

    for (size_t i = 0; i < str->length; i++) {
        str->data[i] = tolower(str->data[i]);
    }
}

/**
 * @brief Replace all occurrences of old_str with new_str in a String.
 * 将字符串中所有出现的旧字符串替换为新字符串。
 * @param str Target string to modify in place. / 要原地修改的目标字符串。
 * @param old_str The substring to be replaced. / 要被替换的子串。
 * @param new_str The replacement substring. / 替换后的子串。
 */
void string_replace(String* str, const String* old_str, const String* new_str) {
    if (!str || !old_str || !new_str || string_is_empty(old_str)) return;

    if (string_index_of_str(str, old_str, 0) == -1) return;

    String* result = string_create_empty();
    if (!result) return;

    size_t pos = 0;
    size_t old_len = old_str->length;
    size_t new_len = new_str->length;

    while (pos < str->length) {
        int found_pos = string_index_of_str(str, old_str, pos);
        if (found_pos == -1) break;

        if (found_pos > pos) {
            string_append_cstr(result, str->data + pos);
            result->data[result->length - (found_pos - pos)] = '\0';
            result->length = found_pos - pos;
        }

        string_append(result, new_str);

        pos = found_pos + old_len;
    }

    if (pos < str->length) {
        string_append_cstr(result, str->data + pos);
    }

    string_set_string(str, result);
    string_destroy(result);
}

/**
 * @brief Replace all occurrences of a C-string with another C-string in a String.
 * 将字符串中所有出现的旧C字符串替换为新C字符串。
 * @param str Target string to modify. / 要修改的目标字符串。
 * @param old_cstr The C-string to be replaced. / 要被替换的C字符串。
 * @param new_cstr The replacement C-string. / 替换后的C字符串。
 */
void string_replace_cstr(String* str, const char* old_cstr, const char* new_cstr) {
    String* old_str = string_create(old_cstr);
    String* new_str = string_create(new_cstr);

    string_replace(str, old_str, new_str);

    string_destroy(old_str);
    string_destroy(new_str);
}

/**
 * @brief Remove leading and trailing whitespace from a String.
 * 去除字符串开头和结尾的空白字符。
 * @param str Target string to trim. / 要去除空格的字符串。
 */
void string_trim(String* str) {
    string_trim_left(str);
    string_trim_right(str);
}

/**
 * @brief Remove leading whitespace from a String.
 * 去除字符串开头的空白字符。
 * @param str Target string to trim. / 要去除开头空格的字符串。
 */
void string_trim_left(String* str) {
    if (!str) return;

    size_t i = 0;
    while (i < str->length && isspace(str->data[i])) {
        i++;
    }

    if (i > 0) {
        memmove(str->data, str->data + i, str->length - i + 1);
        str->length -= i;
    }
}

/**
 * @brief Remove trailing whitespace from a String.
 * 去除字符串结尾的空白字符。
 * @param str Target string to trim. / 要去除结尾空格的字符串。
 */
void string_trim_right(String* str) {
    if (!str) return;

    size_t i = str->length;
    while (i > 0 && isspace(str->data[i - 1])) {
        i--;
    }

    str->data[i] = '\0';
    str->length = i;
}

/**
 * @brief Extract a substring from a String.
 * 从字符串中提取子串。
 * @param str Source string. / 源字符串。
 * @param start Starting index (inclusive). / 起始索引（包含）。
 * @param end Ending index (exclusive). / 结束索引（不包含）。
 * @return A new String containing the substring, or NULL on failure. / 返回包含子串的新字符串，失败返回NULL。
 */
String* string_substring(const String* str, size_t start, size_t end) {
    if (!str || start > end || end > str->length) return NULL;

    size_t sub_length = end - start;
    String* result = string_create_empty();

    if (!string_ensure_capacity(result, sub_length + 1)) {
        string_destroy(result);
        return NULL;
    }

    strncpy(result->data, str->data + start, sub_length);
    result->data[sub_length] = '\0';
    result->length = sub_length;

    return result;
}

/**
 * @brief Find the first occurrence of a character starting from a position.
 * 从指定位置开始查找字符首次出现的位置。
 * @param str The string to search. / 要搜索的字符串。
 * @param c The character to find. / 要查找的字符。
 * @param start The starting index for the search. / 搜索的起始索引。
 * @return Index of the first occurrence, or -1 if not found. / 返回首次出现的索引，未找到返回 -1。
 */
int string_index_of(const String* str, char c, size_t start) {
    if (!str || start >= str->length) return -1;

    for (size_t i = start; i < str->length; i++) {
        if (str->data[i] == c) return i;
    }
    return -1;
}

/**
 * @brief Find the first occurrence of a substring starting from a position.
 * 从指定位置开始查找子串首次出现的位置。
 * @param str The string to search. / 要搜索的字符串。
 * @param substr The substring to find. / 要查找的子串。
 * @param start The starting index for the search. / 搜索的起始索引。
 * @return Index of the first character of the substring, or -1 if not found.
 *         返回子串第一个字符的索引，未找到返回 -1。
 */
int string_index_of_str(const String* str, const String* substr, size_t start) {
    if (!str || !substr || start >= str->length) return -1;

    char* pos = strstr(str->data + start, substr->data);
    if (!pos) return -1;

    return pos - str->data;
}

/**
 * @brief Find the last occurrence of a character, searching backward from a start position.
 * 从指定位置反向查找字符最后一次出现的位置。
 * @param str The string to search. / 要搜索的字符串。
 * @param c The character to find. / 要查找的字符。
 * @param start The index to start the backward search (usually the end or less). / 反向搜索的起始索引。
 * @return Index of the last occurrence, or -1 if not found. / 返回最后一次出现的索引，未找到返回 -1。
 */
int string_last_index_of(const String* str, char c, size_t start) {
    if (!str) return -1;

    if (start >= str->length) start = str->length - 1;

    for (size_t i = start; i != (size_t)-1; i--) {
        if (str->data[i] == c) return i;
    }
    return -1;
}

/**
 * @brief Split a String by a delimiter into an array of strings.
 * 使用分隔符将字符串分割为字符串数组。
 * @param str The string to split. / 待分割的字符串。
 * @param delimiter The delimiter string. / 分隔符字符串。
 * @return A StringArray containing the parts, or NULL on failure. / 返回包含各部分的StringArray，失败返回NULL。
 */
StringArray* string_split(const String* str, const String* delimiter) {
    if (!str || !delimiter || string_is_empty(delimiter)) return NULL;

    StringArray* result = string_array_create();
    size_t start = 0;
    size_t pos = 0;

    while ((pos = string_index_of_str(str, delimiter, start)) != (size_t)-1) {
        String* part = string_substring(str, start, pos);
        string_array_add(result, part);
        string_destroy(part);

        start = pos + delimiter->length;
    }

    if (start < str->length) {
        String* part = string_substring(str, start, str->length);
        string_array_add(result, part);
        string_destroy(part);
    }

    return result;
}

/**
 * @brief Get the character at a given index (unsafe, returns '\0' on error).
 * 获取指定索引处的字符（不安全，出错时返回 '\0'）。
 * @param str Source string. / 源字符串。
 * @param index Position (0-based). / 位置（从0开始）。
 * @return The character, or '\0' if index out of range or str is NULL. / 返回该字符，若索引越界或str为NULL则返回 '\0'。
 */
char string_char_at(const String* str, size_t index) {
    if (!str || index >= str->length) {
        return '\0';
    }
    return str->data[index];
}

/**
 * @brief Safely get the character at a given index with error reporting.
 * 安全地获取指定索引处的字符，并报告错误。
 * @param str Source string. / 源字符串。
 * @param index Position (0-based). / 位置（从0开始）。
 * @param result Pointer to store the character. / 用于存储字符的指针。
 * @return 1 on success, 0 on failure (NULL input or out of range). / 成功返回1，失败返回0（输入为空或越界）。
 */
int string_char_at_safe(const String* str, size_t index, char* result) {
    if (!str || !result) {
        return 0;
    }

    if (index >= str->length) {
        return 0;
    }

    *result = str->data[index];
    return 1;
}

/**
 * @brief Convert a String to an integer.
 * 将字符串转换为整数。
 * @param str Source string. / 源字符串。
 * @param result Pointer to store the integer result. / 用于存储整数的指针。
 * @return 1 on successful conversion, 0 if the string does not represent a valid integer.
 *         转换成功返回1，若字符串不表示有效整数则返回0。
 */
int string_to_int(const String* str, int* result) {
    if (!str || !result) return 0;

    errno = 0;
    char* endptr;
    long value = strtol(str->data, &endptr, 10);

    if (endptr == str->data || *endptr != '\0' || errno == ERANGE) {
        return 0;
    }

    if (value > INT_MAX || value < INT_MIN) {
        return 0;
    }

    *result = (int)value;
    return 1;
}

/**
 * @brief Convert a String to a double.
 * 将字符串转换为双精度浮点数。
 * @param str Source string. / 源字符串。
 * @param result Pointer to store the double result. / 用于存储双精度值的指针。
 * @return 1 on success, 0 if conversion fails or value is infinity/NaN.
 *         成功返回1，若转换失败或值为无穷大/NaN则返回0。
 */
int string_to_double(const String* str, double* result) {
    if (!str || !result) return 0;

    errno = 0;
    char* endptr;
    double value = strtod(str->data, &endptr);

    if (endptr == str->data || *endptr != '\0' || errno == ERANGE) {
        return 0;
    }

    if (isinf(value) || isnan(value)) {
        return 0;
    }

    *result = value;
    return 1;
}

/**
 * @brief Convert a String to a float.
 * 将字符串转换为单精度浮点数。
 * @param str Source string. / 源字符串。
 * @param result Pointer to store the float result. / 用于存储浮点数的指针。
 * @return 1 on success, 0 if out of float range or conversion fails. / 成功返回1，超出范围或转换失败返回0。
 */
int string_to_float(const String* str, float* result) {
    if (!str || !result) return 0;

    double temp;
    if (!string_to_double(str, &temp)) {
        return 0;
    }

    if (temp > FLT_MAX || temp < -FLT_MAX) {
        return 0;
    }

    *result = (float)temp;
    return 1;
}

/**
 * @brief Convert a String to a boolean value.
 * 将字符串转换为布尔值。
 * @param str Source string. / 源字符串。
 * @param result Pointer to store the boolean (0 or 1). / 用于存储布尔值的指针。
 * @return 1 on successful interpretation, 0 if the string does not represent a boolean.
 *         解析成功返回1，若字符串不表示布尔值则返回0。
 */
int string_to_bool(const String* str, int* result) {
    if (!str || !result) return 0;

    String* lower_str = string_clone(str);
    string_to_lower(lower_str);

    int bool_value = 0;

    if (string_equals_cstr(lower_str, "true") ||
        string_equals_cstr(lower_str, "1") ||
        string_equals_cstr(lower_str, "yes") ||
        string_equals_cstr(lower_str, "on")) {
        bool_value = 1;
    }
    else if (string_equals_cstr(lower_str, "false") ||
             string_equals_cstr(lower_str, "0") ||
             string_equals_cstr(lower_str, "no") ||
             string_equals_cstr(lower_str, "off")) {
        bool_value = 0;
    }
    else {
        int num_value;
        if (string_to_int(str, &num_value)) {
            bool_value = (num_value != 0);
        } else {
            string_destroy(lower_str);
            return 0;
        }
    }

    *result = bool_value;
    string_destroy(lower_str);
    return 1;
}

/**
 * @brief Get the first character of a String (as a plain char).
 * 获取字符串的第一个字符（作为普通字符返回）。
 * @param str Source string. / 源字符串。
 * @return The first character, or '\0' if string is NULL or empty. / 返回第一个字符，若为空返回 '\0'。
 */
char string_to_char(const String* str) {
    if (!str || str->length == 0) return '\0';
    return str->data[0];
}

/**
 * @brief Write the content of a String to a file (overwrites existing).
 * 将字符串内容写入文件（覆盖已有内容）。
 * @param str The string to write. / 要写入的字符串。
 * @param filename Path to the file. / 文件路径。
 * @return 1 if all characters were written successfully, 0 on error. / 全部写入成功返回1，出错返回0。
 */
int string_write_to_file(const String* str, const char* filename) {
    if (!str || !filename) return 0;

    FILE* file = fopen(filename, "w");
    if (!file) return 0;

    size_t written = fwrite(str->data, sizeof(char), str->length, file);
    fclose(file);

    return (written == str->length);
}

/**
 * @brief Read the entire content of a file into a new String.
 * 读取文件的全部内容到一个新字符串。
 * @param filename Path to the file. / 文件路径。
 * @return A new String containing the file content, or NULL on failure. / 返回包含文件内容的新字符串，失败返回NULL。
 */
String* string_read_from_file(const char* filename) {
    if (!filename) return NULL;

    FILE* file = fopen(filename, "r");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(file);
        return string_create_empty();
    }

    String* str = string_create_empty();
    if (!str) {
        fclose(file);
        return NULL;
    }

    if (!string_ensure_capacity(str, file_size + 1)) {
        fclose(file);
        string_destroy(str);
        return NULL;
    }

    size_t read_size = fread(str->data, sizeof(char), file_size, file);
    str->data[read_size] = '\0';
    str->length = read_size;

    fclose(file);
    return str;
}

/**
 * @brief Append the content of a String to the end of a file.
 * 将字符串内容追加到文件末尾。
 * @param str The string to append. / 要追加的字符串。
 * @param filename Path to the file. / 文件路径。
 * @return 1 if all characters were written, 0 on error. / 全部写入成功返回1，出错返回0。
 */
int string_append_to_file(const String* str, const char* filename) {
    if (!str || !filename) return 0;

    FILE* file = fopen(filename, "a");
    if (!file) return 0;

    size_t written = fwrite(str->data, sizeof(char), str->length, file);
    fclose(file);

    return (written == str->length);
}

/**
 * @brief Create a formatted string (printf style).
 * 创建格式化字符串（类似printf）。
 * @param format Format string. / 格式字符串。
 * @param ... Additional arguments. / 其他参数。
 * @return A new String containing the formatted result, or NULL on failure. / 返回包含格式化结果的新字符串，失败返回NULL。
 */
String* string_format(const char* format, ...) {
    va_list args;
    va_start(args, format);

    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy) + 1;
    va_end(args_copy);

    if (needed <= 0) {
        va_end(args);
        return NULL;
    }

    String* str = string_create_empty();
    if (!string_ensure_capacity(str, needed)) {
        va_end(args);
        string_destroy(str);
        return NULL;
    }

    vsnprintf(str->data, str->capacity, format, args);
    str->length = strlen(str->data);
    va_end(args);

    return str;
}

/**
 * @brief Print a String to stdout followed by a newline.
 * 将字符串输出到标准输出并换行。
 * @param str The string to print. / 要输出的字符串。
 */
void string_print(const String* str) {
    if (str) {
        printf("%s\n", str->data);
    }
}

/**
 * @brief Create an empty StringArray with default capacity.
 * 创建一个具有默认容量的空字符串数组。
 * @return Pointer to the new StringArray, or NULL on failure. / 返回新StringArray指针，失败返回NULL。
 */
StringArray* string_array_create() {
    return string_array_create_with_capacity(10);
}

/**
 * @brief Create an empty StringArray with a given initial capacity.
 * 创建一个具有指定初始容量的空字符串数组。
 * @param capacity Initial number of slots for strings. / 初始字符串槽位数。
 * @return Pointer to the new StringArray, or NULL on failure. / 返回新StringArray指针，失败返回NULL。
 */
StringArray* string_array_create_with_capacity(size_t capacity) {
    StringArray* array = (StringArray*)malloc(sizeof(StringArray));
    if (!array) return NULL;

    array->items = (String**)malloc(sizeof(String*) * capacity);
    if (!array->items) {
        free(array);
        return NULL;
    }

    array->count = 0;
    array->capacity = capacity;
    return array;
}

/**
 * @brief Destroy a StringArray and free all contained strings.
 * 销毁StringArray并释放所有包含的字符串。
 * @param array The StringArray to destroy. / 要销毁的字符串数组。
 */
void string_array_destroy(StringArray* array) {
    if (array) {
        for (size_t i = 0; i < array->count; i++) {
            string_destroy(array->items[i]);
        }
        free(array->items);
        free(array);
    }
}

/**
 * @brief Add a String to the end of a StringArray.
 * 将字符串添加到字符串数组末尾。
 * @param array The target StringArray. / 目标字符串数组。
 * @param str The string to add (a copy is stored). / 要添加的字符串（会存储副本）。
 */
void string_array_add(StringArray* array, const String* str) {
    if (!array || !str) return;

    if (array->count >= array->capacity) {
        size_t new_capacity = array->capacity * 2;
        String** new_items = (String**)realloc(array->items, sizeof(String*) * new_capacity);
        if (!new_items) return;

        array->items = new_items;
        array->capacity = new_capacity;
    }

    array->items[array->count] = string_clone(str);
    array->count++;
}

/**
 * @brief Add a C-string to the end of a StringArray.
 * 将C字符串添加到字符串数组末尾。
 * @param array The target StringArray. / 目标字符串数组。
 * @param cstr The null-terminated C-string to add. / 要添加的C字符串。
 */
void string_array_add_cstr(StringArray* array, const char* cstr) {
    String* str = string_create(cstr);
    if (str) {
        string_array_add(array, str);
        string_destroy(str);
    }
}

/**
 * @brief Insert a String at a specific index in the array.
 * 在数组的指定索引处插入一个字符串。
 * @param array The target StringArray. / 目标字符串数组。
 * @param index Position where the new string will be placed. / 新字符串插入的位置。
 * @param str The string to insert (a copy is stored). / 要插入的字符串（会存储副本）。
 */
void string_array_insert(StringArray* array, size_t index, const String* str) {
    if (!array || !str || index > array->count) return;

    if (array->count >= array->capacity) {
        size_t new_capacity = array->capacity * 2;
        String** new_items = (String**)realloc(array->items, sizeof(String*) * new_capacity);
        if (!new_items) return;

        array->items = new_items;
        array->capacity = new_capacity;
    }

    for (size_t i = array->count; i > index; i--) {
        array->items[i] = array->items[i - 1];
    }

    array->items[index] = string_clone(str);
    array->count++;
}

/**
 * @brief Remove a String at a given index from the array.
 * 删除数组中指定索引处的字符串。
 * @param array The target StringArray. / 目标字符串数组。
 * @param index Position of the string to remove. / 要删除的字符串的索引。
 */
void string_array_remove(StringArray* array, size_t index) {
    if (!array || index >= array->count) return;

    string_destroy(array->items[index]);

    for (size_t i = index; i < array->count - 1; i++) {
        array->items[i] = array->items[i + 1];
    }

    array->count--;
}

/**
 * @brief Remove all strings from a StringArray (keeps the array itself).
 * 清空字符串数组中的所有字符串（保留数组本身）。
 * @param array The target StringArray. / 目标字符串数组。
 */
void string_array_clear(StringArray* array) {
    if (!array) return;

    for (size_t i = 0; i < array->count; i++) {
        string_destroy(array->items[i]);
    }

    array->count = 0;
}

/**
 * @brief Get a pointer to a String at a given index (no copy).
 * 获取指定索引处的字符串指针（不复制）。
 * @param array The StringArray. / 字符串数组。
 * @param index Position of the desired string. / 所需字符串的索引。
 * @return Pointer to the String, or NULL if out of range. / 返回字符串指针，若越界返回NULL。
 */
String* string_array_get(const StringArray* array, size_t index) {
    if (!array || index >= array->count) return NULL;
    return array->items[index];
}

/**
 * @brief Get the number of strings in a StringArray.
 * 获取字符串数组中的字符串数量。
 * @param array The StringArray. / 字符串数组。
 * @return Number of strings, or 0 if array is NULL. / 返回字符串数量，若数组为NULL返回0。
 */
size_t string_array_size(const StringArray* array) {
    return array ? array->count : 0;
}

/**
 * @brief Check if a StringArray is empty.
 * 检查字符串数组是否为空。
 * @param array The StringArray. / 字符串数组。
 * @return 1 if empty or NULL, 0 otherwise. / 若为空或NULL返回1，否则返回0。
 */
int string_array_is_empty(const StringArray* array) {
    return !array || array->count == 0;
}

/**
 * @brief Print the contents of a StringArray to stdout in a readable format.
 * 以可读格式将字符串数组的内容输出到标准输出。
 * @param array The StringArray to print. / 要输出的字符串数组。
 */
void string_array_print(const StringArray* array) {
    if (!array) return;

    printf("[");
    for (size_t i = 0; i < array->count; i++) {
        printf("\"%s\"", array->items[i]->data);
        if (i < array->count - 1) {
            printf(", ");
        }
    }
    printf("]\n");
}

/**
 * @brief Join all strings in a StringArray using a separator.
 * 使用分隔符将字符串数组中的所有字符串连接成一个字符串。
 * @param array The StringArray to join. / 要连接的字符串数组。
 * @param separator The separator string placed between elements. / 元素之间放置的分隔符。
 * @return A new String containing the joined result, or empty string on error. / 返回包含连接结果的新字符串，出错返回空字符串。
 */
String* string_array_join(const StringArray* array, const String* separator) {
    if (!array || array->count == 0) return string_create_empty();
    if (!separator) return string_create_empty();

    String* result = string_create_empty();
    if (!result) return NULL;

    for (size_t i = 0; i < array->count; i++) {
        string_append(result, array->items[i]);
        if (i < array->count - 1) {
            string_append(result, separator);
        }
    }

    return result;
}

/**
 * @brief Write each string in a StringArray to a file, separated by a delimiter.
 * 将字符串数组中的每个字符串写入文件，用分隔符分隔。
 * @param array The StringArray to write. / 要写入的字符串数组。
 * @param filename Path to the output file. / 输出文件路径。
 * @param delimiter Delimiter string between elements (default newline if NULL). / 元素间的分隔符（若为NULL则默认为换行）。
 * @return 1 on success, 0 on failure. / 成功返回1，失败返回0。
 */
int string_array_write_to_file(const StringArray* array, const char* filename, const char* delimiter) {
    if (!array || !filename) return 0;

    FILE* file = fopen(filename, "w");
    if (!file) return 0;

    const char* delim = delimiter ? delimiter : "\n";
    size_t delim_len = strlen(delim);

    int success = 1;
    for (size_t i = 0; i < array->count; i++) {
        size_t written = fwrite(array->items[i]->data, sizeof(char), array->items[i]->length, file);
        if (written != array->items[i]->length) {
            success = 0;
            break;
        }

        if (i < array->count - 1) {
            written = fwrite(delim, sizeof(char), delim_len, file);
            if (written != delim_len) {
                success = 0;
                break;
            }
        }
    }

    fclose(file);
    return success;
}

/**
 * @brief Read a file and split its content by a delimiter into a StringArray.
 * 读取文件内容并用分隔符分割为字符串数组。
 * @param filename Path to the file. / 文件路径。
 * @param delimiter Delimiter string used for splitting (default newline if NULL). / 用于分割的分隔符（若为NULL则默认为换行）。
 * @return A new StringArray, or NULL on failure. / 返回新的StringArray，失败返回NULL。
 */
StringArray* string_array_read_from_file(const char* filename, const char* delimiter) {
    if (!filename) return NULL;

    String* file_content = string_read_from_file(filename);
    if (!file_content) return NULL;

    const char* delim = delimiter ? delimiter : "\n";
    String* delim_str = string_create(delim);
    StringArray* result = string_split(file_content, delim_str);

    string_destroy(file_content);
    string_destroy(delim_str);

    return result;
}

/**
 * @brief Read a line from standard input (stdin) as a new String (newline stripped).
 * 从标准输入读取一行作为新字符串（去除换行符）。
 * @return A new String containing the line, or NULL on EOF/error. / 返回包含该行的新字符串，EOF或错误返回NULL。
 */
String* string_read_line() {
    String* str = string_create_empty();
    if (!str) return NULL;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), stdin)) {
        string_append_cstr(str, buffer);

        if (str->length > 0 && str->data[str->length - 1] == '\n') {
            str->data[str->length - 1] = '\0';
            str->length--;

            if (str->length > 0 && str->data[str->length - 1] == '\r') {
                str->data[str->length - 1] = '\0';
                str->length--;
            }
            break;
        }
    }

    if (string_is_empty(str)) {
        string_destroy(str);
        return NULL;
    }

    return str;
}

/**
 * @brief Read a line from stdin after displaying a prompt.
 * 显示提示信息后从标准输入读取一行。
 * @param prompt The prompt string to display (may be NULL). / 要显示的提示字符串（可为NULL）。
 * @return A new String containing the line, or NULL on EOF/error. / 返回包含该行的新字符串，EOF或错误返回NULL。
 */
String* string_read_line_with_prompt(const char* prompt) {
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }
    return string_read_line();
}

/**
 * @brief Read a line from stdin into an existing String (newline stripped).
 * 从标准输入读取一行存入已有的字符串（去除换行符）。
 * @param str The String to store the input (must not be NULL). / 用于存储输入的字符串（不能为NULL）。
 * @return 1 on success, 0 on failure or EOF. / 成功返回1，失败或EOF返回0。
 */
int string_read_line_into(String* str) {
    if (!str) return 0;

    string_clear(str);

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), stdin)) {
        string_append_cstr(str, buffer);

        if (str->length > 0 && str->data[str->length - 1] == '\n') {
            str->data[str->length - 1] = '\0';
            str->length--;

            if (str->length > 0 && str->data[str->length - 1] == '\r') {
                str->data[str->length - 1] = '\0';
                str->length--;
            }
            return 1;
        }
    }

    return 0;
}