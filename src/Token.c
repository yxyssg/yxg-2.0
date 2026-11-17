#include "Token.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "Error.h"

static struct key_token key_token[] = {
    {"=",TOKEN_ASSIGN},{"+",TOKEN_ADD},{"-",TOKEN_SUB},
    {"*",TOKEN_MUL},{"/",TOKEN_DIV},{";",TOKEN_SEMICOLON},
    {"print",TOKEN_PRINT},{",",TOKEN_COMMA},{".",TOKEN_DOT},{"..",TOKEN_DOT_DOT},
    {"int",TOKEN_INT},{"string",TOKEN_STRING},{"float",TOKEN_FLOAT},
    {"bool", TOKEN_BOOL},{"char",TOKEN_CHAR}, {"if", TOKEN_IF},
    {"while",TOKEN_WHILE},{"for",TOKEN_FOR},{"do",TOKEN_DO},{"else",TOKEN_ELSE},
    {"break",TOKEN_BREAK},{"continue",TOKEN_CONTINUE},{"{",TOKEN_BRACKET_LEFT},{"}",TOKEN_BRACKET_RIGHT},
    {"==",TOKEN_EQ},{"!=",TOKEN_NE},{">",TOKEN_GT},{"<",TOKEN_LT},{"!",TOKEN_OPPOSE},
    {">=",TOKEN_GE},{"<=",TOKEN_LE},{"(",TOKEN_PAREN_LEFT},{")",TOKEN_PAREN_RIGHT},
    {"switch",TOKEN_SWITCH},{"case",TOKEN_CASE},{"default",TOKEN_DEFAULT},{":",TOKEN_COLON},
    {"%",TOKEN_MOD},{"pub",TOKEN_PUB},{"pro",TOKEN_PRO},{"pri",TOKEN_PRI},
    {"void",TOKEN_VOID},{"return",TOKEN_RETURN},{"[", TOKEN_SQUARE_BRACKET_LEFT},
    {"]", TOKEN_SQUARE_BRACKET_RIGHT},{"new", TOKEN_NEW},{"uint", TOKEN_UINT},
    {"ufloat", TOKEN_UFLOAT},{"&&", TOKEN_LOGICAL_AND},{"||", TOKEN_LOGICAL_OR},{"&",  TOKEN_BITWISE_AND},
    {"|",  TOKEN_BITWISE_OR},{"~",  TOKEN_BITWISE_NOT},{"++", TOKEN_INCREMENT},
    {"--", TOKEN_DECREMENT},{"+=", TOKEN_ADD_ASSIGN},{"-=", TOKEN_SUB_ASSIGN},
    {"*=", TOKEN_MUL_ASSIGN},{"/=", TOKEN_DIV_ASSIGN},{"%=", TOKEN_MOD_ASSIGN},
    {"input", TOKEN_INPUT},{"class", TOKEN_CLASS},{"enum",  TOKEN_ENUM},
    {"this",  TOKEN_THIS},{"import",TOKEN_IMPORT},{"as",TOKEN_AS},
    {"<<", TOKEN_SHIFT_LEFT},{">>", TOKEN_SHIFT_RIGHT},{"<<=", TOKEN_SHIFT_LEFT_ASSIGN},
    {">>=", TOKEN_SHIFT_RIGHT_ASSIGN},{"^",  TOKEN_BITWISE_XOR},{"^=", TOKEN_BITWISE_XOR_ASSIGN},
    {"?",TOKEN_QUESTION},{"null",TOKEN_NULL},{"unsafe",TOKEN_UNSAFE},{"static",TOKEN_STATIC},
    {"sizeof",TOKEN_SIZEOF},{"str",TOKEN_STR},{"->", TOKEN_ARROW},{"...",TOKEN_VARARG},{"InputSet", TOKEN_INPUT_SET},
    {"try",TOKEN_TRY},{"catch",TOKEN_CATCH},{"throw",TOKEN_THROW},{"end",TOKEN_END},{"native",TOKEN_NATIVE},
    {"virtual",TOKEN_VIRTUAL},{"override",TOKEN_OVERRIDE},{"const",TOKEN_CONST},

};

static int key_length = sizeof(key_token)/sizeof(struct key_token);

struct key_token find_key(char* code)
{
    for(int i = 0; i < key_length; i++)
    {
        if (strcmp(code,key_token[i].value) == 0)
        {
            return key_token[i];
        }
    }
    return find_number(code);
}

bool is_key_symbol(char code)
{
    switch (code)
    {
        case '+':
        case '-':
        case '*':
        case '/':
        case '=':
        case '!':
        case '>':
        case '<':
        case '%':
        case ';':
        case ':':
        case ',':
        case '.':
        case '(':
        case ')':
        case '{':
        case '}':
        case '[':
        case ']':
        case '&':
        case '|':
        case '~':
        case '^':
    case '?':
            return true;
        default:
            return false;
    }
}

struct key_token find_number(char* code)
{
    if (code == NULL || code[0] == '\0') {
        struct key_token token = {code, TOKEN_BOOL};
        return token;
    }

    if (code[0] == '0' && (code[1] == 'x' || code[1] == 'X')) {
        int i = 2;
        if (!isxdigit(code[i])) return find_bool(code);
        while (isxdigit(code[i])) i++;
        if (code[i] != '\0') return find_bool(code);
        struct key_token token = {code, TOKEN_NUMBER};
        return token;
    }

    if (code[0] == '0' && (code[1] == 'b' || code[1] == 'B')) {
        int i = 2;
        if (code[i] != '0' && code[i] != '1') return find_bool(code);
        while (code[i] == '0' || code[i] == '1') i++;
        if (code[i] != '\0') return find_bool(code);
        struct key_token token = {code, TOKEN_NUMBER};
        return token;
    }

    if (code[0] == '0' && code[1] != '\0'
        && code[1] != 'x' && code[1] != 'X'
        && code[1] != 'b' && code[1] != 'B'
        && code[1] != '.') {
        int i = 1;
        if (code[i] < '0' || code[i] > '7') return find_bool(code);
        while (code[i] >= '0' && code[i] <= '7') i++;
        if (code[i] != '\0') return find_bool(code);
        struct key_token token = {code, TOKEN_NUMBER};
        return token;
    }

    int i = 0;
    if (code[i] == '+' || code[i] == '-') {
        i++;
        if (code[i] == '\0') {
            return find_bool(code);
        }
    }

    int digit_count = 0;
    while (code[i] != '\0' && isdigit(code[i])) {
        digit_count++;
        i++;
    }

    if (code[i] == '\0' && digit_count > 0) {
        struct key_token token = {code, TOKEN_NUMBER};
        return token;
    }

    return find_float_number(code);
}

struct key_token find_float_number(char* code) {
    if (code == NULL || code[0] == '\0') {
        struct key_token token = {code, TOKEN_BOOL};
        return token;
    }

    int i = 0;
    int has_digit = 0;
    int has_dot = 0;
    int has_e = 0;

    if (code[i] == '+' || code[i] == '-') {
        i++;
        if (code[i] == '\0') {
            return find_bool(code);
        }
    }

    while (code[i] != '\0' && isdigit(code[i])) {
        has_digit = 1;
        i++;
    }

    if (code[i] == '.') {
        has_dot = 1;
        i++;

        if (code[i] != '\0' && isdigit(code[i])) {
            while (code[i] != '\0' && isdigit(code[i])) {
                has_digit = 1;
                i++;
            }
        } else if (!has_digit) {
            return find_bool(code);
        }
    }

    if (has_digit && (code[i] == 'e' || code[i] == 'E')) {
        has_e = 1;
        i++;

        if (code[i] == '+' || code[i] == '-') {
            i++;
        }

        int exp_digit_count = 0;
        while (code[i] != '\0' && isdigit(code[i])) {
            exp_digit_count++;
            i++;
        }

        if (exp_digit_count == 0) {
            return find_bool(code);
        }
    }

    if (code[i] != '\0') {
        return find_bool(code);
    }

    if (has_digit && (has_dot || has_e)) {
        struct key_token token = {code, TOKEN_FLOAT_LITERAL};
        return token;
    }

    return find_bool(code);
}

struct key_token find_bool(char* code)
{
    if(strcmp(code,"true") == 0 || strcmp(code,"1") == 0 ||
        strcmp(code, "false") == 0 || strcmp(code, "0") == 0)
    {
        struct key_token token = {code,TOKEN_BOOL_LITERAL};
        return token;
    }
    return find_identifier(code);
}

struct key_token find_identifier(char* code)
{
    static const char* prefixes[] = {"int", "uint", "float", "ufloat"};
    static const int widths[] = {8, 16, 32, 64, 128};
    for (int p = 0; p < 4; p++) {
        int plen = strlen(prefixes[p]);
        if (strncmp(code, prefixes[p], plen) != 0) continue;
        const char* rest = code + plen;
        if (rest[0] == '\0') break;
        for (int w = 0; w < 5; w++) {
            char wbuf[8];
            snprintf(wbuf, sizeof(wbuf), "%d", widths[w]);
            if (strcmp(rest, wbuf) != 0) continue;
            TokenType t = TOKEN_IDENTIFIER;
            switch (p) {
            case 0:
                if (widths[w]==8) t=TOKEN_INT8; else if (widths[w]==16) t=TOKEN_INT16;
                else if (widths[w]==32) t=TOKEN_INT32; else if (widths[w]==64) t=TOKEN_INT64;
                else if (widths[w]==128) t=TOKEN_INT128;
                break;
            case 1:
                if (widths[w]==8) t=TOKEN_UINT8; else if (widths[w]==16) t=TOKEN_UINT16;
                else if (widths[w]==32) t=TOKEN_UINT32; else if (widths[w]==64) t=TOKEN_UINT64;
                else if (widths[w]==128) t=TOKEN_UINT128;
                break;
            case 2:
                if (widths[w]==8) t=TOKEN_FLOAT8; else if (widths[w]==16) t=TOKEN_FLOAT16;
                else if (widths[w]==32) t=TOKEN_FLOAT32; else if (widths[w]==64) t=TOKEN_FLOAT64;
                else if (widths[w]==128) t=TOKEN_FLOAT128;
                break;
            case 3:
                if (widths[w]==8) t=TOKEN_UFLOAT8; else if (widths[w]==16) t=TOKEN_UFLOAT16;
                else if (widths[w]==32) t=TOKEN_UFLOAT32; else if (widths[w]==64) t=TOKEN_UFLOAT64;
                else if (widths[w]==128) t=TOKEN_UFLOAT128;
                break;
            }
            struct key_token token = {code, t};
            return token;
        }
    }

    struct key_token token = {code,TOKEN_IDENTIFIER};
    return token;
}

bool is_number_prefix(const char* s)
{
    if (s == NULL || *s == '\0') return false;
    if (*s == '-') s++;
    if (*s == '\0') return false;
    for (; *s != '\0'; s++) {
        if (!isdigit((unsigned char)*s)) return false;
    }
    return true;
}

int parse_integer(const char* s) {
    if (!s) return 0;
    if (s[0] == '0') {
        if (s[1] == 'x' || s[1] == 'X') {
            return (int)strtoul(s + 2, NULL, 16);
        }
        if (s[1] == 'b' || s[1] == 'B') {
            return (int)strtol(s + 2, NULL, 2);
        }
        if (s[1] != '\0' && s[1] != '.') {
            return (int)strtol(s, NULL, 8);
        }
    }
    return (int)strtol(s, NULL, 10);
}

long long parse_integer64(const char* s) {
    if (!s) return 0;
    unsigned long long val = 0;
    if (s[0] == '0') {
        if (s[1] == 'x' || s[1] == 'X') {
            val = strtoull(s + 2, NULL, 16);
            return (long long)val;
        }
        if (s[1] == 'b' || s[1] == 'B') {
            val = strtoull(s + 2, NULL, 2);
            return (long long)val;
        }
        if (s[1] != '\0' && s[1] != '.') {
            val = strtoull(s, NULL, 8);
            return (long long)val;
        }
    }
    val = strtoull(s, NULL, 10);
    return (long long)val;
}

char* normalize_number_str(const char* s) {
    if (!s || !*s) {
        return s ? strdup(s) : NULL;
    }

    int base = 10;
    bool neg = false;
    const char* p = s;

    if (*p == '-' || *p == '+') {
        neg = (*p == '-');
        p++;
    }

    if (p[0] == '0') {
        if (p[1] == 'x' || p[1] == 'X') {
            base = 16;
        } else if (p[1] == 'b' || p[1] == 'B') {
            base = 2;
        } else if (p[1] != '\0' && p[1] != '.') {
            base = 8;
        }
    }

    unsigned long long uval = strtoull(p, NULL, base);
    long long val = neg ? -(long long)uval : (long long)uval;

    if (val >= -2147483648LL && val <= 2147483647LL) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", val);
        return strdup(buf);
    }
    return strdup(s);
}

char* type_to_string(TokenType type) {
    switch (type)
    {
        case TOKEN_PLACEHOLDER:
        return "PLACEHOLDER";
        case TOKEN_NUMBER:
        return "Number";
        case TOKEN_INT:
        return "Int";
        case TOKEN_STRING:
        return "String";
        case TOKEN_STR:
        return "Str";
        case TOKEN_CHAR:
        return "Char";
        case TOKEN_BOOL:
        return "Bool";
        case TOKEN_FLOAT:
        return "Float";
        case TOKEN_UINT:
        return "UInt";
        case TOKEN_UFLOAT:
        return "UFloat";
        case TOKEN_INT8:
        return "Int8";
        case TOKEN_INT16:
        return "Int16";
        case TOKEN_INT32:
        return "Int32";
        case TOKEN_INT64:
        return "Int64";
        case TOKEN_INT128:
        return "Int128";
        case TOKEN_UINT8:
        return "UInt8";
        case TOKEN_UINT16:
        return "UInt16";
        case TOKEN_UINT32:
        return "UInt32";
        case TOKEN_UINT64:
        return "UInt64";
        case TOKEN_UINT128:
        return "UInt128";
        case TOKEN_FLOAT8:
        return "Float8";
        case TOKEN_FLOAT16:
        return "Float16";
        case TOKEN_FLOAT32:
        return "Float32";
        case TOKEN_FLOAT64:
        return "Float64";
        case TOKEN_FLOAT128:
        return "Float128";
        case TOKEN_UFLOAT8:
        return "UFloat8";
        case TOKEN_UFLOAT16:
        return "UFloat16";
        case TOKEN_UFLOAT32:
        return "UFloat32";
        case TOKEN_UFLOAT64:
        return "UFloat64";
        case TOKEN_UFLOAT128:
        return "UFloat128";
        case TOKEN_VOID:
        return "Void";
        case TOKEN_FLOAT_LITERAL:
        return "Float_Literal";
        case TOKEN_BOOL_LITERAL:
        return "Bool_Literal";
        case TOKEN_CHAR_LITERAL:
        return "Char_Literal";
        case TOKEN_STRING_LITERAL:
        return "String_Literal";
        case TOKEN_IDENTIFIER:
        return "Identifier";
        case TOKEN_ASSIGN:
        return "Assignment";
        case TOKEN_CONST:
        return "Const";
        case TOKEN_ARROW:
       return "Arrow";
        case TOKEN_PRINT:
        return "Print";
        case TOKEN_SWITCH:
        return "Switch";
        case TOKEN_CASE:
        return "Case";
        case TOKEN_DEFAULT:
        return "Default";
        case TOKEN_ADD:
        return "Addition";
        case TOKEN_SUB:
        return "Subtraction";
        case TOKEN_MUL:
        return "Multiplication";
        case TOKEN_DIV:
        return "Division";
        case TOKEN_MOD:
        return "Modulo";
        case TOKEN_SEMICOLON:
        return "Semicolon";
        case TOKEN_COMMA:
        return "Comma";
        case TOKEN_DOT:
        return "Dot";
        case TOKEN_DOT_DOT:
        return "DotDot";
        case TOKEN_VARARG:
        return "Vararg";
        case TOKEN_QUESTION:
        return "Question";
        case TOKEN_COLON:
        return "Colon";
        case TOKEN_EQ:
        return "Equal";
        case TOKEN_NE:
        return "NEqual";
        case TOKEN_GT:
        return "Greater";
        case TOKEN_LT:
        return "Less";
        case TOKEN_GE:
        return "GreaterEqual";
        case TOKEN_LE:
        return "LessEqual";
        case TOKEN_OPPOSE:
        return "Oppose";
        case TOKEN_PAREN_LEFT:
        return "ParenLeft";
        case TOKEN_PAREN_RIGHT:
        return "ParenRight";
        case TOKEN_BRACKET_LEFT:
        return "BracketLeft";
        case TOKEN_BRACKET_RIGHT:
        return "BracketRight";
        case TOKEN_SQUARE_BRACKET_LEFT:
        return "SquareBracketLeft";
        case TOKEN_SQUARE_BRACKET_RIGHT:
        return "SquareBracketRight";
        case TOKEN_LOGICAL_AND:
        return "LogicalAnd";
        case TOKEN_LOGICAL_OR:
        return "LogicalOr";
        case TOKEN_BITWISE_AND:
        return "BitwiseAnd";
        case TOKEN_BITWISE_OR:
        return "BitwiseOr";
        case TOKEN_BITWISE_NOT:
        return "BitwiseNot";
        case TOKEN_SHIFT_LEFT:
        return "ShiftLeft";
        case TOKEN_SHIFT_RIGHT:
        return "ShiftRight";
        case TOKEN_SHIFT_LEFT_ASSIGN:
        return "ShiftLeftAssign";
        case TOKEN_SHIFT_RIGHT_ASSIGN:
        return "ShiftRightAssign";
        case TOKEN_BITWISE_XOR:
        return "BitwiseXor";
        case TOKEN_BITWISE_XOR_ASSIGN:
        return "BitwiseXorAssign";
        case TOKEN_INCREMENT:
        return "Increment";
        case TOKEN_DECREMENT:
        return "Decrement";
        case TOKEN_ADD_ASSIGN:
        return "AddAssign";
        case TOKEN_SUB_ASSIGN:
        return "SubAssign";
        case TOKEN_MUL_ASSIGN:
        return "MulAssign";
        case TOKEN_DIV_ASSIGN:
        return "DivAssign";
        case TOKEN_MOD_ASSIGN:
        return "ModAssign";
        case TOKEN_INPUT:
        return "Input";
        case TOKEN_INPUT_SET:
        return "InputSet";
        case TOKEN_DO:
        return "DO";
        case TOKEN_IF:
        return "If";
        case TOKEN_ELSE:
        return "Else";
        case TOKEN_WHILE:
        return "While";
        case TOKEN_FOR:
        return "For";
        case TOKEN_CONTINUE:
        return "Continue";
        case TOKEN_BREAK:
        return "Break";
        case TOKEN_PUB:
        return "Pub";
        case TOKEN_PRI:
        return "Pri";
        case TOKEN_PRO:
        return "Pro";
        case TOKEN_RETURN:
        return "Return";
        case TOKEN_NEW:
        return "New";
        case TOKEN_THIS:
        return "This";
        case TOKEN_CLASS:
        return "Class";
        case TOKEN_ENUM:
        return "Enum";
        case TOKEN_IMPORT:
        return "Import";
        case TOKEN_AS:
        return "As";
        case TOKEN_NULL:
        return "Null";
        case TOKEN_SIZEOF:
        return "Sizeof";
        case TOKEN_STATIC:
        return "Static";
        case TOKEN_NATIVE:
        return "Native";
        case TOKEN_TRY:
        return "Try";
        case TOKEN_CATCH:
        return "Catch";
        case TOKEN_THROW:
        return "Throw";
        case TOKEN_END:
        return "End";
        case TOKEN_UNSAFE:
        return "Unsafe";
        case TOKEN_VIRTUAL:
        return "Virtual";
        case TOKEN_OVERRIDE:
        return "Override";
        case TOKEN_ERROR:
        return "Error";
        case TOKEN_NONE:
        return "None";
        case TOKEN_EOF:
        return "EOF";
    }
}