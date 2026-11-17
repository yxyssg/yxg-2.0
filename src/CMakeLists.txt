cmake_minimum_required(VERSION 3.20)
project(yxg C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# YXG 编译器本身是 64 位，但它生成的代码是 32 位（x86-32）
# 编译器进程不需要 -m32；-m32 只用于编译生成的 output.asm
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-O2 -std=c23 -Wall -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-function)
endif()

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
)

# 编译器运行时要从 exe 所在目录找 runtime/ runtime_cache/ lib/
# 构建后自动复制过去，方便直接运行
add_custom_command(TARGET yxg POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/runtime
            $<TARGET_FILE_DIR:yxg>/runtime
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/runtime_cache
            $<TARGET_FILE_DIR:yxg>/runtime_cache
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/lib
            $<TARGET_FILE_DIR:yxg>/lib
    COMMENT "Copying runtime and lib next to yxg"
)
