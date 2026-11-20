cmake_minimum_required(VERSION 3.20)
project(yxg C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(yxg
        main.c
        Token.c
        Token.h
        Lexer.c
        Lexer.h
        YString.h
        YString.c
        parser.c
        parser.h
        AST.h
        Semantics.c
        Semantics.h
        Codegen.h
        Error.h
        Error.c
        IR.c
        IR.h
        Codegen_win32.c
        regmap.c
        regmap.h
        CFG.c
        CFG.h
        SSA.c
        SSA.h
        dwmap.c
        dwmap.h
)