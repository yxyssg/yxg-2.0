#ifndef TOKEN_H
#define TOKEN_H

typedef enum TokenType
{
    TOKEN_PLACEHOLDER,

    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_UINT,
    TOKEN_UFLOAT,
    TOKEN_INT8,
    TOKEN_INT16,
    TOKEN_INT32,
    TOKEN_INT64,
    TOKEN_INT128,
    TOKEN_UINT8,
    TOKEN_UINT16,
    TOKEN_UINT32,
    TOKEN_UINT64,
    TOKEN_UINT128,
    TOKEN_FLOAT8,
    TOKEN_FLOAT16,
    TOKEN_FLOAT32,
    TOKEN_FLOAT64,
    TOKEN_FLOAT128,
    TOKEN_UFLOAT8,
    TOKEN_UFLOAT16,
    TOKEN_UFLOAT32,
    TOKEN_UFLOAT64,
    TOKEN_UFLOAT128,
    TOKEN_STRING,
    TOKEN_STR,
    TOKEN_CHAR,
    TOKEN_BOOL,
    TOKEN_VOID,

    TOKEN_FLOAT_LITERAL,
    TOKEN_STRING_LITERAL,
    TOKEN_CHAR_LITERAL,
    TOKEN_BOOL_LITERAL,

    TOKEN_ASSIGN,
    TOKEN_ADD,
    TOKEN_SUB,
    TOKEN_MUL,
    TOKEN_DIV,
    TOKEN_MOD,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_DOT,
    TOKEN_DOT_DOT,
    TOKEN_VARARG,
    TOKEN_ARROW,
    TOKEN_QUESTION,
    TOKEN_EQ,
    TOKEN_OPPOSE,
    TOKEN_NE,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_LE,
    TOKEN_GE,
    TOKEN_PAREN_LEFT,
    TOKEN_PAREN_RIGHT,
    TOKEN_BRACKET_LEFT,
    TOKEN_BRACKET_RIGHT,
    TOKEN_SQUARE_BRACKET_LEFT,
    TOKEN_SQUARE_BRACKET_RIGHT,
    TOKEN_LOGICAL_AND,
    TOKEN_LOGICAL_OR,
    TOKEN_BITWISE_AND,
    TOKEN_BITWISE_OR,
    TOKEN_BITWISE_NOT,
    TOKEN_SHIFT_LEFT,
    TOKEN_SHIFT_RIGHT,
    TOKEN_SHIFT_LEFT_ASSIGN,
    TOKEN_SHIFT_RIGHT_ASSIGN,
    TOKEN_BITWISE_XOR,
    TOKEN_BITWISE_XOR_ASSIGN,
    TOKEN_INCREMENT,
    TOKEN_DECREMENT,
    TOKEN_ADD_ASSIGN,
    TOKEN_SUB_ASSIGN,
    TOKEN_MUL_ASSIGN,
    TOKEN_DIV_ASSIGN,
    TOKEN_MOD_ASSIGN,

    TOKEN_PRINT,
    TOKEN_INPUT,
    TOKEN_INPUT_SET,
    TOKEN_IF,
    TOKEN_SWITCH,
    TOKEN_CASE,
    TOKEN_DEFAULT,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_DO,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_PUB,
    TOKEN_PRO,
    TOKEN_PRI,
    TOKEN_CONST,
    TOKEN_NATIVE,
    TOKEN_LINK,
    TOKEN_STDCALL,
    TOKEN_VIRTUAL,
    TOKEN_OVERRIDE,
    TOKEN_RETURN,
    TOKEN_NEW,
    TOKEN_CLASS,
    TOKEN_ENUM,
    TOKEN_THIS,
    TOKEN_IMPORT,
    TOKEN_AS,
    TOKEN_NULL,
    TOKEN_UNSAFE,
    TOKEN_STATIC,
    TOKEN_SIZEOF,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_THROW,
    TOKEN_END,

    TOKEN_ERROR,
    TOKEN_NONE,
    TOKEN_EOF,
}TokenType;

typedef struct Token
{
    char* value;
    TokenType type;
    int line;
    int column;
    const char* file;
}Token;

struct key_token
{
    char* value;
    TokenType type;
};

struct key_token find_identifier(char* code);
struct key_token find_number(char* code);
struct key_token find_float_number(char* code);
struct key_token find_bool(char* code);
struct key_token find_key(char* code);
bool is_number_prefix(const char* s);
char* type_to_string(TokenType type);
bool is_key_symbol(char code);
char* normalize_number_str(const char* s);
int parse_integer(const char* s);
long long parse_integer64(const char* s);

#endif
