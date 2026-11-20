#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Codegen.h"
#include "IR.h"
#include "Lexer.h"
#include "parser.h"
#include "Semantics.h"
#include "CFG.h"


#ifdef _WIN32
#define TokenType  Win32_TokenType_avoid_conflict
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#undef TokenType
#undef ERROR
#define MAKE_DIR(p) _mkdir(p)
#define YXG_PATH_MAX MAX_PATH
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define YXG_PATH_MAX PATH_MAX
#define MAKE_DIR(p) mkdir((p), 0755)
#endif

#include "Error.h"
#include "SSA.h"
#include "dwmap.h"

typedef struct BuildOptions {
    const char** files;
    int file_count;
    int file_capacity;
    char* output;
    bool run;
    bool only_asm;
    bool only_window;
    bool assertions;
    bool force_rebuild;
    char* dump_symbols;
    bool debug_info;
} BuildOptions;
typedef struct Dependency {
    char* name;
    char* git_url;
    char* version;
    char* version_kind;
} Dependency;

typedef struct DependencyList {
    Dependency* items;
    int count;
    int capacity;
} DependencyList;

typedef struct ResolvedDep {
    char* name;
    char* path;
    struct ResolvedDep* next;
} ResolvedDep;

typedef struct ProjectConfig {
    char* name;
    char* version;
    char* entry;
    char* output_path;
    char* config_dir;
    char* config_path;
    char* build_flags;
    bool loaded;
    DependencyList dependencies;
    ResolvedDep* resolved;
} ProjectConfig;

static bool is_directory(const char* path);
static void add_input_file(BuildOptions* opts, const char* path);
static void scan_dir_recursive(BuildOptions* opts, const char* dir);
static void expand_inputs(BuildOptions* opts);
static void sort_inputs(BuildOptions* opts);
static void free_build_options(BuildOptions* opts);

static char* path_join(const char* base, const char* rel);
static char* find_config_file(char** out_dir);
static void parse_config(const char* path, ProjectConfig* cfg);
static int load_project_config(ProjectConfig* cfg, const char* explicit_path);
static void free_project_config(ProjectConfig* cfg);
static int cmd_compile_common(int argc, char** argv, bool force_run);

static void ensure_dir_recursive(const char* dir);
static void ensure_parent_dir(const char* file_path);
static bool stat_file_mtime(const char* path, time_t* out);
static bool needs_rebuild(BuildOptions* opts, ProjectConfig* cfg);

static int runtime_obj(const char* name);
static char* append_to_command(char* command, const char* suffix);
static char* append_runtime_to_command(char* command, const char* name, const char* extra);

static void dep_list_add(DependencyList* list, Dependency* dep);
static void free_dependency_list(DependencyList* list);
static void free_resolved_deps(ResolvedDep* d);
static int parse_inline_table(char* s, Dependency* dep);
static void get_cache_dir(char* buf, size_t bufsize);
static int download_dependency(Dependency* dep);
static void resolve_dependencies_recursive(DependencyList* deps, ResolvedDep** out, int depth);
static int resolve_dependencies(ProjectConfig* cfg);
static void collect_dependency_sources(BuildOptions* opts, ProjectConfig* cfg);

static int run_compiler(BuildOptions* opts);
static int cmd_build(int argc, char** argv);
static int cmd_run(int argc, char** argv);
static int cmd_clean(void);
static int cmd_new(int argc, char** argv);
static int parse_build_args(int argc, char** argv, BuildOptions* opts);

static char g_exe_dir[MAX_PATH] = "";
static char g_runtime_dir[MAX_PATH] = "";
static char g_runtime_cache[MAX_PATH] = "";

bool checkFileSuffix(const char *filename, const char *suffix) {
    if (filename == NULL || suffix == NULL || *filename == '\0' || *suffix == '\0') {
        ERROR(ERR_PARSE_MODULE_NOT_FOUND, LOC_NONE, "file name cannot be empty");
        return false;
    }

    const char *dot = strrchr(filename, '.');
    if (dot == NULL) {
        return false;
    }

    if (strcmp(dot + 1, suffix) == 0) {
        return true;
    } else {
        return false;
    }
}

char* read_source(char* file_name)
{
    if (file_name == NULL) {
        ERROR(ERR_PARSE_MODULE_NOT_FOUND, LOC_NONE, "file name cannot be empty");
        return NULL;
    }
    if (!checkFileSuffix(file_name, "yxg")) {
        ERROR(ERR_PARSE_MODULE_NOT_FOUND, LOC(file_name, 0, 0), "file type must be .yxg");
        return NULL;
    }
    String* string = string_read_from_file(file_name);
    if (!string || string_is_empty(string)) {
        ERROR(ERR_PARSE_MODULE_NOT_FOUND, LOC(file_name, 0, 0), "file is empty");
        if (string) string_destroy(string);
        return NULL;
    }
    char* temp = malloc(string_length(string) + 1);
    strcpy(temp, string_cstr(string));
    string_destroy(string);
    return temp;
}

void print_help()
{
    printf("Usage: yxg <command> [options] [files|dirs...]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  build [options] [files|dirs...]   Compile project\n");
    printf("  run   [options] [files|dirs...]   Compile and run\n");
    printf("  clean                             Remove build artifacts\n");
    printf("  new   <name>                      Create a new project\n");
    printf("  help                              Show this help\n");
    printf("\n");
    printf("Options:\n");
    printf("  -o <path>       Output executable path\n");
    printf("  -r              Compile and run (same as 'run')\n");
    printf("  -a              Emit assembly only\n");
    printf("  -w              Windowed-only program (no console)\n");
    printf("  -noassert       Disable assert checks\n");
    printf("  --force         Force rebuild (ignore timestamps)\n");
    printf("  -d              Debug mode\n");
    printf("\n");
    printf("Others:\n");
    printf("  -time           Show current time\n");
    printf("  -version        Show current version\n");
    printf("\n");
    printf("Legacy single-shot usage:\n");
    printf("  yxg <files...> -o <out> -r\n");
}

static int run_compiler(BuildOptions* opts)
{
    clock_t start_time = clock();
    char* output_file_name = opts->output;
    bool is_executable = opts->run;
    bool only_asm = opts->only_asm;
    bool only_window = opts->only_window;
    assertions_enabled = opts->assertions;
    debug_info_enabled = opts->debug_info;

    char* file_name = NULL;
    if (opts->file_count > 0) {
        file_name = (char*)opts->files[opts->file_count - 1];
    }

    if (output_file_name != NULL) {
        char* p = output_file_name;
        while (*p) {
            if (*p == '/') *p = '\\';
            p++;
        }
        ensure_parent_dir(output_file_name);
    }
    {
        char exe_dir[MAX_PATH] = "";
#ifdef _WIN32
        GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
        char* last_sep = strrchr(exe_dir, '\\');
        if (last_sep) *last_sep = '\0';
#else
        ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
        if (len != -1) {
            exe_dir[len] = '\0';
            char* last_sep = strrchr(exe_dir, '/');
            if (last_sep) *last_sep = '\0';
        }
#endif
        strncpy(g_exe_dir, exe_dir, MAX_PATH - 1);
        snprintf(g_runtime_dir, MAX_PATH, "%s%cruntime",
                 exe_dir, '\\');
        snprintf(g_runtime_cache, MAX_PATH, "%s%cruntime_cache",
                 exe_dir, '\\');
        ensure_dir_recursive(g_runtime_cache);
        module_path_add_defaults(exe_dir);
    }

    Lexer* lexer_instances[256] = {NULL};
    int lexer_count = 0;

    ASTNode* ast_node = NULL;
    ASTNode* ast_tail = NULL;
    for (int i = 0; i < opts->file_count; i++) {
        const char* input = opts->files[i];
        char* abs = realpath_wrapper(input);
        if (abs && is_already_imported(abs)) {
            free(abs);
            continue;
        }
        if (abs) {
            mark_imported(abs);
        }
        char* src = read_source((char*)input);
        if (!src) {
            ERROR(ERR_PARSE_MODULE_NOT_FOUND, LOC(input, 0, 0), "cannot read '%s'", input);
            continue;
        }
        Lexer* lex = lexer(src, input);
        if (lexer_count < 256) {
            lexer_instances[lexer_count++] = lex;
        }
        // for (int j = 0;j < lex->size; j++)
        // {
        //     printf("value:%s,type:%s\n",lex->tokens[j].value,type_to_string(lex->tokens[j].type));
        // }
        Parser* p = parser_init(lex->tokens, input);
        ASTNode* file_ast = parser_parse(p);
        if (!file_ast) continue;

        if (!ast_node) {
            ast_node = file_ast;
            ast_tail = file_ast;
        } else {
            ast_tail->next = file_ast;
        }
        while (ast_tail->next) ast_tail = ast_tail->next;
    }

    if (!ast_node) {
        FATAL(ERR_INTERNAL, LOC_NONE, "no input files");
    }

    // Print_AST(ast_node,0,false);
    if (has_error)
    {
        clock_t end_time = clock();
        double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        fprintf(stderr,"BUILD FAILED in %.6fs\n",elapsed_time);
        AST_Free(ast_node);
        for (int i = 0; i < lexer_count; i++) lexer_free(lexer_instances[i]);
        module_system_cleanup();
        semantics_cleanup();
        codegen_cleanup();
        exit(EXIT_FAILURE);
    }
    if (file_name) {
        set_current_semantic_file(file_name);
    }
    ast_node = semantics_prepare_program(ast_node);
    Semantics(ast_node);
    semantics_propagate_closures(ast_node);

    if (opts->dump_symbols != NULL) {
        FILE* sym_out = fopen(opts->dump_symbols, "w");
        if (sym_out == NULL) {
            fprintf(stderr, "error: cannot write %s\n", opts->dump_symbols);
        } else {
            semantics_dump_symbols_json(ast_node,
                                        opts->files && opts->file_count > 0
                                            ? opts->files[0] : "",
                                        sym_out);
            fclose(sym_out);
        }
        AST_Free(ast_node);
        for (int i = 0; i < lexer_count; i++) lexer_free(lexer_instances[i]);
        module_system_cleanup();
        semantics_cleanup();
        codegen_cleanup();
        return 0;
    }
    // Print_AST(ast_node,0,true);

    if (has_error)
    {
        clock_t end_time = clock();
        double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        fprintf(stderr,"BUILD FAILED in %.6fs\n",elapsed_time);
        AST_Free(ast_node);
        for (int i = 0; i < lexer_count; i++) lexer_free(lexer_instances[i]);
        module_system_cleanup();
        semantics_cleanup();
        codegen_cleanup();
        return 1;
    }

    ASTNode** pp = &ast_node;
    while (*pp) {
        ASTNode* node = *pp;
        if (node->type == AST_FUNCTION
            && ((AST_Function*)node)->is_generic
            && ((AST_Function*)node)->internal_name != NULL
            && strchr(((AST_Function*)node)->internal_name, '$') == NULL) {
            *pp = node->next;
            node->next = NULL;
            AST_Free(node);
        } else {
            pp = &node->next;
        }
    }

    IR* ir = parse_IR(ast_node);
    ASTNode* inst_funcs = semantics_get_instantiated_functions();
    if (inst_funcs) {
        IR* inst_ir = parse_IR(inst_funcs);
        if (inst_ir) {
            IR* tail = inst_ir;
            while (tail->next) tail = tail->next;
            tail->next = ir;
            ir = inst_ir;
        }
    }
    // print_IR(ir);

    IR* full_ir_head = NULL;
    IR* full_ir_tail = NULL;

    regmap_init();
    FuncIR* func_list = ir_split_by_function(ir);
    for (FuncIR* fi = func_list; fi; fi = fi->next) {
        if (fi->func_name && strcmp(fi->func_name, "__preamble__") == 0 ||
            strcmp(fi->func_name, "__interlude__") == 0) {
            if (!full_ir_head) {
                full_ir_head = fi->first_ir;
            } else {
                full_ir_tail->next = fi->first_ir;
            }
            full_ir_tail = fi->last_ir;
            while (full_ir_tail->next) full_ir_tail = full_ir_tail->next;
            continue;
        }
        // printf("\n╔══ 函数: %s ═══════════════════╗\n",
        //        fi->func_name ? fi->func_name : "(unnamed)");
        CFG* cfg = cfg_build(fi->first_ir, fi->last_ir);
        if (!cfg) continue;

        cfg_remove_unreachable(cfg);
        if (cfg->block_count == 0) {
            cfg_destroy(cfg);
            continue;
        }

        cfg_assign_linear_ids(cfg);

        cfg_compute_dominators(cfg);
        cfg_compute_dominance_frontiers(cfg);

        ssa_construct(cfg);
        // ssa_print_cfg(cfg);

        for (int round = 0; round < 4; round++) {
            int changes = 0;
            changes += ssa_constant_folding(cfg);
            changes += ssa_dead_code_elimination(cfg);
            if (changes == 0) break;
        }

        insert_gc_clears(cfg);
        IR* func_ir = ssa_destruct(cfg);
        if (fi->func_name && strcmp(fi->func_name, "worker_c") == 0) {
            fprintf(stderr, "===== worker_c after SSA =====\n");
            print_IR(func_ir);
            fprintf(stderr, "===== end =====\n");
        }
        if (fi->func_name && strcmp(fi->func_name, "__preamble__") == 0
            || fi->func_name && strcmp(fi->func_name, "__interlude__") == 0) {
            fprintf(stderr, "===== %s =====\n", fi->func_name);
            print_IR(func_ir);
            fprintf(stderr, "===== end =====\n");
        }
        // cfg_print(cfg);
        cfg_destroy(cfg);
        if (!func_ir) continue;

        ssa_strip_versions(func_ir);

        CFG* cfg2 = cfg_build(func_ir, NULL);
        if (cfg2) {
            cfg_remove_unreachable(cfg2);
            cfg_assign_linear_ids(cfg2);
            ssa_local_value_numbering(cfg2);
            regmap_allocate_from_cfg(cfg2, 5);
            regmap_store_for_function(fi->func_name, g_regmap);
            cfg_destroy(cfg2);
        }

        if (!full_ir_head) {
            full_ir_head = func_ir;
        } else {
            full_ir_tail->next = func_ir;
        }
        full_ir_tail = func_ir;
        while (full_ir_tail->next) full_ir_tail = full_ir_tail->next;
        // printf("╚══════════════════════════════════╝\n\n");
    }
    func_ir_list_free(func_list);

    // printf("\n=== Final IR (after SSA) ===\n");
    // print_IR(full_ir_head);
    // printf("=== End Final IR ===\n\n");

    FILE* output = fopen("output.asm", "w");
    codegen_win32_init();
    codegen_win32(output, full_ir_head);
    codegen_win32_end(output);
    fclose(output);

    free_IR_chain(full_ir_head);

    if (only_asm)
    {
        clock_t end_time = clock();
        double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        fprintf(stderr, "Compilation complete, output file is: output.asm\n");
        fprintf(stderr, "Total compilation time: %.6f second\n", elapsed_time);
        return 0;
    }

    if (has_print)
    {
        if (runtime_obj("runtime_print") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_gc)
    {
        if (runtime_obj("runtime_gc") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_unsafe_lib)
    {
        if (runtime_obj("runtime_unsafe") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_64bit_ops)
    {
        if (runtime_obj("runtime_int64") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_string_lib)
    {
        if (runtime_obj("runtime_string") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_memory_lib)
    {
        if (runtime_obj("runtime_memory") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_float_lib)
    {
        if (runtime_obj("runtime_float") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_array_lib)
    {
        if (runtime_obj("runtime_array") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (main_has_args || has_input || has_format || has_system_lib)
    {
        if (runtime_obj("runtime_system") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_bounds_check)
    {
        if (runtime_obj("runtime_bounds") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_exception_lib)
    {
        if (runtime_obj("runtime_exception") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_file_lib)
    {
        if (runtime_obj("runtime_file") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_socket_lib)
    {
        if (runtime_obj("runtime_iocp") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
        if (runtime_obj("runtime_socket") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    /* 2.5.1 FFI: 有用户 extern 声明时链接动态加载支持 */
    /* 2.5.3 HTTPS: 有 _tls_* 调用时链接 SChannel 支持 */
    if (has_tls_lib)
    {
        if (runtime_obj("runtime_tls") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (semantics_user_extern_count() > 0)
    {
        if (runtime_obj("runtime_ffi") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_debug_lib)
    {
        if (runtime_obj("runtime_debug") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_window_lib)
    {
        if (runtime_obj("runtime_window") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_time_lib)
    {
        if (runtime_obj("runtime_time") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_thread_lib)
    {
        if (runtime_obj("runtime_thread") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_sync_lib)
    {
        if (runtime_obj("runtime_sync") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_function_lib)
    {
        if (runtime_obj("runtime_function") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }

    /* runtime_window.asm 里的 Canvas_load 会引用 _image_load，
       所以只要链 runtime_window 就必须有 image_loader.obj，不只是用了图片才需要。 */
    if (has_image_lib || has_window_lib)
    {
        char nasm_cmd[MAX_PATH * 4];
        snprintf(nasm_cmd, sizeof(nasm_cmd),
                 "if not exist image_loader.obj gcc -m32 -O2 -DSTBI_NO_SIMD -c \"%s\\image_loader.c\" -o image_loader.obj",
                 g_runtime_dir);
        if (system(nasm_cmd) != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_music_lib)
    {
        char nasm_cmd[MAX_PATH * 4];
        snprintf(nasm_cmd, sizeof(nasm_cmd),
                 "if not exist music_loader.obj gcc -m32 -O2 -c \"%s\\music_loader.c\" -o music_loader.obj",
                 g_runtime_dir);
        if (system(nasm_cmd) != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
        char cmd[MAX_PATH * 4];
        snprintf(cmd, sizeof(cmd),
                 "if not exist music_loader.obj gcc -m32 -O2 -c \"%s\\music_loader.c\" -o music_loader.obj",
                 g_exe_dir);
        if (system(cmd) != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
        if (runtime_obj("runtime_music") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }
    if (has_video_lib)
    {
        char nasm_cmd[MAX_PATH * 4];
        snprintf(nasm_cmd, sizeof(nasm_cmd),
                 "if not exist video_loader.obj gcc -g -m32 -O2 -c \"%s\\video_loader.c\" -o video_loader.obj",
                 g_runtime_dir);
        if (system(nasm_cmd) != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
        char cmd[MAX_PATH * 4];
        snprintf(cmd, sizeof(cmd),
                 "if not exist video_loader.obj gcc -g -m32 -O2 -c \"%s\\video_loader.c\" -o video_loader.obj",
                 g_exe_dir);
        if (system(cmd) != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
        if (runtime_obj("runtime_video") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }

    if (has_sqlite)
    {
        char sqlite_obj[MAX_PATH * 2];
        snprintf(sqlite_obj, sizeof(sqlite_obj), "%s\\sqlite3.obj", g_runtime_dir);
        if (GetFileAttributesA(sqlite_obj) == INVALID_FILE_ATTRIBUTES)
        {
            char sqlite_src[MAX_PATH * 2];
            snprintf(sqlite_src, sizeof(sqlite_src), "%s\\sqlite3.c", g_runtime_dir);
            char build_cmd[MAX_PATH * 4];
            snprintf(build_cmd, sizeof(build_cmd),
                     "gcc -m32 -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OS_WIN=1 -c \"%s\" -o \"%s\"",
                     sqlite_src, sqlite_obj);
            if (system(build_cmd) != 0)
            {
                clock_t end_time = clock();
                double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
                fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
                return 1;
            }
        }
        if (runtime_obj("runtime_sqlite") != 0)
        {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            fprintf(stderr, "Build Failed in %.6fs!\n", elapsed_time);
            return 1;
        }
    }

    /* 2.5.0：-g 时同时生成 listing，用于提取「偏移 -> 行号」映射 */
    const char* nasm_cmd = debug_info_enabled
        ? "nasm -g output.asm -O1 -f win32 -o output.obj -l output.lst"
        : "nasm -g output.asm -O1 -f win32 -o output.obj";
    if (system(nasm_cmd) != 0)
    {
        clock_t end_time = clock();
        double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        fprintf(stderr, "Build Failed in %.6fs!\n",elapsed_time);
        return 1;
    }

    char* command = "gcc -m32 -g -fuse-ld=lld";
    if (has_print) command = append_runtime_to_command(command, "runtime_print", NULL);
    if (has_gc) command = append_runtime_to_command(command, "runtime_gc", NULL);
    if (has_unsafe_lib) command = append_runtime_to_command(command, "runtime_unsafe", NULL);
    if (has_64bit_ops) command = append_runtime_to_command(command, "runtime_int64", NULL);
    if (has_string_lib) command = append_runtime_to_command(command, "runtime_string", NULL);
    if (has_memory_lib) command = append_runtime_to_command(command, "runtime_memory", NULL);
    if (has_float_lib) command = append_runtime_to_command(command, "runtime_float", NULL);
    if (has_array_lib) command = append_runtime_to_command(command, "runtime_array", NULL);
    if (main_has_args || has_input || has_format || has_system_lib) command = append_runtime_to_command(command, "runtime_system", NULL);
    if (has_bounds_check) command = append_runtime_to_command(command, "runtime_bounds", NULL);
    if (has_exception_lib) command = append_runtime_to_command(command, "runtime_exception", NULL);
    if (has_file_lib) command = append_runtime_to_command(command, "runtime_file", NULL);
    if (has_socket_lib) {
        command = append_runtime_to_command(command, "runtime_iocp", NULL);
        command = append_runtime_to_command(command, "runtime_socket", "-lws2_32 -lmswsock");

    }
    if (has_window_lib) {
        if (only_window) {
            command = append_runtime_to_command(command, "runtime_window",
                "-mwindows -Wl,-e,_mainCRTStartup -luser32 -lgdi32 -lcomdlg32 -lcomctl32 -luxtheme");
        } else {
            command = append_runtime_to_command(command, "runtime_window",
                "-Wl,-e,_mainCRTStartup -luser32 -lgdi32 -lcomdlg32 -lcomctl32 -luxtheme");
        }
    }
    if (has_image_lib || has_window_lib) command = append_to_command(command, "image_loader.obj");
    if (has_music_lib) command = append_runtime_to_command(command, "runtime_music", "-lole32 -lwinmm -luuid");
    if (has_debug_lib) command = append_runtime_to_command(command, "runtime_debug", NULL);
    if (has_tls_lib) command = append_runtime_to_command(command, "runtime_tls", "-lsecur32");
    if (semantics_user_extern_count() > 0) command = append_runtime_to_command(command, "runtime_ffi", NULL);
    if (has_video_lib) command = append_runtime_to_command(command, "runtime_video", "video_loader.obj music_loader.obj -lmfplat -lmfreadwrite -lmfuuid -lole32 -luuid -lwinmm");
    if (has_time_lib) command = append_runtime_to_command(command, "runtime_time", NULL);
    if (has_thread_lib) command = append_runtime_to_command(command, "runtime_thread", NULL);
    if (has_sync_lib) command = append_runtime_to_command(command, "runtime_sync", NULL);
    if (has_function_lib) command = append_runtime_to_command(command, "runtime_function", NULL);
    if (has_music_lib && !has_video_lib) command = append_to_command(command, "music_loader.obj");
    if (has_sqlite)
    {
        command = append_runtime_to_command(command, "runtime_sqlite", NULL);
        char sqlite_obj[MAX_PATH * 2];
        snprintf(sqlite_obj, sizeof(sqlite_obj), "%s\\sqlite3.obj", g_runtime_dir);
        command = append_to_command(command, sqlite_obj);
    }

    if (has_window_lib)
    {
        static const char* manifest_text =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
            "  <dependency>\n"
            "    <dependentAssembly>\n"
            "      <assemblyIdentity\n"
            "          type=\"win32\"\n"
            "          name=\"Microsoft.Windows.Common-Controls\"\n"
            "          version=\"6.0.0.0\"\n"
            "          processorArchitecture=\"*\"\n"
            "          publicKeyToken=\"6595b64144ccf1df\"\n"
            "          language=\"*\"/>\n"
            "    </dependentAssembly>\n"
            "  </dependency>\n"
            "</assembly>\n";

        FILE* mf = fopen("yxg_app.manifest", "w");
        if (!mf) {
            fprintf(stderr, "WARNING: cannot write yxg_app.manifest\n");
        } else {
            fputs(manifest_text, mf);
            fclose(mf);
        }

        FILE* rf = fopen("yxg_app.rc", "w");
        if (!rf) {
            fprintf(stderr, "WARNING: cannot write yxg_app.rc\n");
        } else {
            fputs("1 24 \"yxg_app.manifest\"\n", rf);
            fclose(rf);
        }

        bool manifest_ok = false;
        if (system("windres yxg_app.rc -o yxg_app.o") == 0) {
            manifest_ok = true;
        } else {
            fprintf(stderr, "WARNING: windres failed, no visual styles (flat toolbar may stick)\n");
        }

        if (manifest_ok) {
            int len = snprintf(NULL, 0, "%s yxg_app.o", command);
            char* temp = malloc(len + 1);
            if (temp) {
                snprintf(temp, len + 1, "%s yxg_app.o", command);
            }
            command = strdup(temp);
            free(temp);
        }
    }

    int len = snprintf(NULL, 0, "%s output.obj -o %s",command,output_file_name);
    char* temp = malloc(len + 1);
    if (temp) {
        snprintf(temp, len + 1, "%s output.obj -o %s",command,output_file_name);
    }
    command = strdup(temp);
    free(temp);
    /* 2.5.1 FFI: link "xxx.dll"; 声明的依赖库。必须放在 output.obj 之后，
       否则链接器先看到 -lxxx、再看到需要它的目标文件，符号解析不到。 */
    for (int _li = 0; _li < yxg_link_lib_count(); _li++) {
        const char* _ln = yxg_link_lib_at(_li);
        if (!_ln) continue;
        char _lbuf[128];
        snprintf(_lbuf, sizeof(_lbuf), " -l%s", _ln);
        command = append_to_command(command, _lbuf);
    }

    if (system(command) != 0)
    {
        clock_t end_time = clock();
        double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        fprintf(stderr, "Build Failed in %.6fs!\n",elapsed_time);
        remove(output_file_name);
        return 1;
    }

    /* 2.5.0：-g 时生成「地址 -> 行号」映射 */
    if (debug_info_enabled) {
        char map_path[MAX_PATH * 2];
        snprintf(map_path, sizeof(map_path), "%s.map", output_file_name);
        const char* src_leaf = file_name;
        if (src_leaf) {
            const char* s1 = strrchr(src_leaf, '\\');
            const char* s2 = strrchr(src_leaf, '/');
            if (s1 && (!s2 || s1 > s2)) src_leaf = s1 + 1;
            else if (s2) src_leaf = s2 + 1;
        }
        dwmap_generate("output.lst", output_file_name, map_path, src_leaf);
    }

    AST_Free(ast_node);
    /* 退出前不做完整 teardown：
       1) 进程马上结束，OS 会回收，不释放不影响正确性；
       2) 实测这些 free 会碰到已损坏的堆元数据，导致 0xC0000005
          （表现为"编译成功但没有任何输出"）。
       3) 这里只把输出刷干净，保证提示一定可见。 */
    fflush(stdout);
    fflush(stderr);
    clock_t end_time = clock();

    double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    fprintf(stderr, "Compilation complete, output file is: %s\n", output_file_name);
    fprintf(stderr, "Total compilation time: %.6f second\n", elapsed_time);

    // remove("output.asm");
    // remove("output.obj");
    // remove("runtime_print.obj");
    // remove("runtime_string.obj");
    // remove("runtime_array.obj");
    // remove("runtime_bounds.obj");
    // remove("runtime_exception.obj");
    // remove("runtime_float.obj");
    // remove("runtime_system.obj");
    // remove("runtime_gc.obj");
    // remove("runtime_memory.obj");
    // remove("runtime_unsafe.obj");
    // remove("runtime_int64.obj");
    // remove("runtime_file.obj");
    // remove("runtime_socket.obj");
    // remove("runtime_window.obj");
    // remove("image_loader.obj");
    // remove("runtime_music.obj");
    // remove("music_loader.obj");
    // remove("runtime_video.obj");
    // remove("runtime_time.obj");
    // remove("video_loader.obj");
    // remove("yxg_app.o");
    // remove("yxg_app.rc");
    // remove("yxg_app.manifest");
    remove("output.asm");
    remove("output.obj");
    remove("image_loader.obj");
    remove("runtime_music.obj");
    remove("music_loader.obj");
    remove("runtime_video.obj");
    remove("video_loader.obj");
    remove("sqlite3.obj");
    remove("yxg_app.o");
    remove("yxg_app.rc");
    remove("yxg_app.manifest");

    if (is_executable)
    {
        FILE* probe = fopen(output_file_name, "rb");
        if (!probe) {
            fprintf(stderr, "error: built executable not found: %s\n", output_file_name);
            fprintf(stderr, "       (build may have failed, or the output path is not writable)\n");
        } else {
            fclose(probe);
            fflush(stdout);
            fflush(stderr);
            char run_cmd[MAX_PATH * 2];
            snprintf(run_cmd, sizeof(run_cmd), "\"%s\"", output_file_name);
            fprintf(stderr, "run: %s\n", output_file_name);
            fflush(stderr);
            int run_rc = system(run_cmd);
            unsigned int run_code = (unsigned int)run_rc;
            if (run_rc == 0) {
                fprintf(stderr, "run: exit code 0\n");
            } else {
                fprintf(stderr, "run: exit code %d (0x%08X)\n", run_rc, run_code);
                if (run_code >= 0x80000000u) {
                    fprintf(stderr, "run: program crashed (0x%08X is usually an access violation)\n", run_code);
                    fprintf(stderr, "     try: yxg build <file>, then run the exe by hand\n");
                }
            }
        }
    }

    if (error_occurred()) {
        fprintf(stderr, "\n%d error(s), %d warning(s)\n",
                error_count(), warning_count());
    }
    error_cleanup();
    return error_occurred() ? 1 : 0;
}

static bool is_directory(const char* path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void add_input_file(BuildOptions* opts, const char* path)
{
    if (opts->file_count >= opts->file_capacity) {
        int nc = opts->file_capacity ? opts->file_capacity * 2 : 16;
        const char** nf = realloc(opts->files, sizeof(char*) * nc);
        if (!nf) OUT_OF_MEMORY();
        opts->files = nf;
        opts->file_capacity = nc;
    }
    opts->files[opts->file_count++] = strdup(path);
}

static void scan_dir_recursive(BuildOptions* opts, const char* dir)
{
#ifdef _WIN32
    char pattern[YXG_PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char full[YXG_PATH_MAX];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_dir_recursive(opts, full);
        } else {
            const char* dot = strrchr(fd.cFileName, '.');
            if (dot && strcmp(dot, ".yxg") == 0) {
                add_input_file(opts, full);
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    DIR* d = opendir(dir);
    if (!d) return;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char full[YXG_PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);

        if (is_directory(full)) {
            scan_dir_recursive(opts, full);
        } else {
            const char* dot = strrchr(ent->d_name, '.');
            if (dot && strcmp(dot, ".yxg") == 0) {
                add_input_file(opts, full);
            }
        }
    }
    closedir(d);
#endif
}

static void expand_inputs(BuildOptions* opts)
{
    const char** raw = opts->files;
    int raw_count = opts->file_count;

    opts->files = NULL;
    opts->file_count = 0;
    opts->file_capacity = 0;

    for (int i = 0; i < raw_count; i++) {
        if (is_directory(raw[i])) {
            scan_dir_recursive(opts, raw[i]);
        } else {
            add_input_file(opts, raw[i]);
        }
        free((char*)raw[i]);
    }
    free(raw);
}

static int cmp_str(const void* a, const void* b)
{
    return strcmp(*(const char**)a, *(const char**)b);
}

static void sort_inputs(BuildOptions* opts)
{
    if (opts->file_count > 1) {
        qsort((void*)opts->files, (size_t)opts->file_count, sizeof(char*), cmp_str);
    }
}

static void free_build_options(BuildOptions* opts)
{
    if (opts->files) {
        for (int i = 0; i < opts->file_count; i++) {
            free((char*)opts->files[i]);
        }
        free(opts->files);
        opts->files = NULL;
        opts->file_count = 0;
        opts->file_capacity = 0;
    }
    free(opts->output);
    opts->output = NULL;
}

static int build_merged_argv(int argc, char** argv, const char* flags_str,
                             int* out_argc, char*** out_argv)
{
    int extra = 0;
    if (flags_str && *flags_str) {
        const char* p = flags_str;
        while (*p) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            extra++;
            while (*p && *p != ' ' && *p != '\t') p++;
        }
    }

    int total = argc + extra;
    char** merged = malloc(sizeof(char*) * (total + 1));
    if (!merged) OUT_OF_MEMORY();

    int i = 0;
    merged[i++] = argv[0];

    if (flags_str && *flags_str) {
        char* copy = strdup(flags_str);
        char* p = copy;
        while (*p) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            char* start = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
            merged[i++] = strdup(start);
        }
        free(copy);
    }

    for (int j = 1; j < argc; j++) {
        merged[i++] = argv[j];
    }
    merged[i] = NULL;

    *out_argc = i;
    *out_argv = merged;
    return extra;
}

static int parse_build_args(int argc, char** argv, BuildOptions* opts)
{
    memset(opts, 0, sizeof(*opts));
    opts->output = NULL;
    opts->assertions = true;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -o requires an argument\n");
                return -1;
            }
            free(opts->output);
            opts->output = strdup(argv[++i]);
            if (!opts->output) OUT_OF_MEMORY();
        } else if (strcmp(argv[i], "-r") == 0) {
            opts->run = true;
        } else if (strcmp(argv[i], "-a") == 0) {
            opts->only_asm = true;
        } else if (strcmp(argv[i], "-w") == 0) {
            opts->only_window = true;
        } else if (strcmp(argv[i], "-noassert") == 0) {
            opts->assertions = false;
        } else if (strcmp(argv[i], "-d") == 0) {
            /* 保留兼容，暂未使用 */
        } else if (strcmp(argv[i], "--force") == 0) {
            opts->force_rebuild = true;
        } else if (argv[i][0] != '-') {
            add_input_file(opts, argv[i]);
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--debug-info") == 0) {
            opts->debug_info = true;
        } else if (strcmp(argv[i], "--dump-symbols") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --dump-symbols requires a path\n");
                return -1;
            }
            free(opts->dump_symbols);
            opts->dump_symbols = strdup(argv[++i]);
        } else {
            fprintf(stderr, "warning: unknown option '%s'\n", argv[i]);
        }
    }
    return 0;
}

static int cmd_build(int argc, char** argv)
{
    return cmd_compile_common(argc, argv, false);
}

static int cmd_run(int argc, char** argv)
{
    return cmd_compile_common(argc, argv, true);
}


static void ensure_dir_recursive(const char* dir)
{
    if (!dir || !*dir) return;
    if (is_directory(dir)) return;

    char buf[YXG_PATH_MAX];
    size_t len = strlen(dir);
    if (len >= sizeof(buf)) return;
    memcpy(buf, dir, len + 1);

    while (len > 1 && (buf[len - 1] == '/' || buf[len - 1] == '\\')) {
        buf[--len] = '\0';
    }

    for (size_t i = 1; i < len; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
#ifdef _WIN32
            if (i == 2 && buf[1] == ':') continue;
#endif
            char saved = buf[i];
            buf[i] = '\0';
            MAKE_DIR(buf);
            buf[i] = saved;
        }
    }
    MAKE_DIR(buf);
}

static void ensure_parent_dir(const char* file_path)
{
    if (!file_path) return;

    const char* sep1 = strrchr(file_path, '/');
    const char* sep2 = strrchr(file_path, '\\');
    const char* sep = (sep1 > sep2) ? sep1 : sep2;
    if (!sep) return;

    size_t dlen = (size_t)(sep - file_path);
    if (dlen == 0) return;

    char dir[YXG_PATH_MAX];
    if (dlen >= sizeof(dir)) return;
    memcpy(dir, file_path, dlen);
    dir[dlen] = '\0';

    ensure_dir_recursive(dir);
}


static bool stat_file_mtime(const char* path, time_t* out)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    *out = st.st_mtime;
    return true;
}

static bool needs_rebuild(BuildOptions* opts, ProjectConfig* cfg)
{
    const char* check_path = opts->only_asm ? "output.asm" : opts->output;

    time_t out_mtime;
    if (!stat_file_mtime(check_path, &out_mtime)) {
        return true;
    }

    for (int i = 0; i < opts->file_count; i++) {
        time_t in_mtime;
        if (!stat_file_mtime(opts->files[i], &in_mtime)) {
            return true;
        }
        if (in_mtime > out_mtime) {
            return true;
        }
    }

    if (cfg->loaded && cfg->config_path) {
        time_t cfg_mtime;
        if (!stat_file_mtime(cfg->config_path, &cfg_mtime)) {
            return true;
        }
        if (cfg_mtime > out_mtime) {
            return true;
        }
    }

    return false;
}


static int runtime_obj(const char* name)
{
    char src_path[MAX_PATH * 2];
    char obj_path[MAX_PATH * 2];

    snprintf(src_path, sizeof(src_path), "%s\\%s.asm", g_runtime_dir, name);
    snprintf(obj_path, sizeof(obj_path), "%s\\%s.obj", g_runtime_cache, name);

    time_t src_mtime, obj_mtime;
    if (stat_file_mtime(src_path, &src_mtime) &&
        stat_file_mtime(obj_path, &obj_mtime) &&
        obj_mtime >= src_mtime) {
        return 0;
        }

    char cmd[MAX_PATH * 4];
    snprintf(cmd, sizeof(cmd),
             "nasm -g -f win32 -O0 \"%s\" -o \"%s\"",
             src_path, obj_path);
    if (system(cmd) != 0) {
        return -1;
    }
    return 0;
}

static char* append_to_command(char* command, const char* suffix)
{
    int len = snprintf(NULL, 0, "%s %s", command, suffix);
    char* temp = malloc(len + 1);
    if (!temp) OUT_OF_MEMORY();
    snprintf(temp, len + 1, "%s %s", command, suffix);
    char* result = strdup(temp);
    free(temp);
    return result;
}

static char* append_runtime_to_command(char* command, const char* name, const char* extra)
{
    char obj_path[MAX_PATH * 2];
    snprintf(obj_path, sizeof(obj_path), "%s\\%s.obj", g_runtime_cache, name);

    if (extra && *extra) {
        int len = snprintf(NULL, 0, "%s \"%s\" %s", command, obj_path, extra);
        char* temp = malloc(len + 1);
        if (!temp) OUT_OF_MEMORY();
        snprintf(temp, len + 1, "%s \"%s\" %s", command, obj_path, extra);
        char* result = strdup(temp);
        free(temp);
        return result;
    } else {
        int len = snprintf(NULL, 0, "%s \"%s\"", command, obj_path);
        char* temp = malloc(len + 1);
        if (!temp) OUT_OF_MEMORY();
        snprintf(temp, len + 1, "%s \"%s\"", command, obj_path);
        char* result = strdup(temp);
        free(temp);
        return result;
    }
}

static char* path_join(const char* base, const char* rel)
{
    if (!base || !*base) return rel ? strdup(rel) : NULL;
    if (!rel || !*rel) return strdup(base);
    if (rel[0] == '/' || rel[0] == '\\') return strdup(rel);
#ifdef _WIN32
    if (rel[1] == ':') return strdup(rel);
#endif

    size_t blen = strlen(base);
    while (blen > 1 && (base[blen - 1] == '/' || base[blen - 1] == '\\')) {
        blen--;
    }

    size_t rlen = strlen(rel);
    char* result = malloc(blen + 1 + rlen + 1);
    if (!result) OUT_OF_MEMORY();

    memcpy(result, base, blen);
    result[blen] = '/';
    memcpy(result + blen + 1, rel, rlen + 1);
    return result;
}

static char* find_config_file(char** out_dir)
{
    char cwd[YXG_PATH_MAX];
#ifdef _WIN32
    if (_getcwd(cwd, sizeof(cwd)) == NULL) return NULL;
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL) return NULL;
#endif

    size_t len = strlen(cwd);
    while (len > 1 && (cwd[len - 1] == '/' || cwd[len - 1] == '\\')) {
        cwd[--len] = '\0';
    }

    char current[YXG_PATH_MAX];
    strncpy(current, cwd, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';

    while (1) {
        char candidate[YXG_PATH_MAX];
#ifdef _WIN32
        snprintf(candidate, sizeof(candidate), "%s\\yxg.toml", current);
#else
        snprintf(candidate, sizeof(candidate), "%s/yxg.toml", current);
#endif

        FILE* f = fopen(candidate, "r");
        if (f) {
            fclose(f);
            if (out_dir) *out_dir = strdup(current);
            return strdup(candidate);
        }

        char* sep1 = strrchr(current, '/');
        char* sep2 = strrchr(current, '\\');
        char* sep = (sep1 > sep2) ? sep1 : sep2;

        if (!sep || sep == current) break;
#ifdef _WIN32
        if (sep == current + 2 && current[1] == ':') break;
#endif

        *sep = '\0';
    }

    return NULL;
}

static void trim_in_place(char* s)
{
    char* p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);

    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'
                       || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

static void parse_config(const char* path, ProjectConfig* cfg)
{
    FILE* f = fopen(path, "r");
    if (!f) return;

    char line[1024];
    char section[64] = "";

    while (fgets(line, sizeof(line), f)) {
        char* comment = strchr(line, '#');
        if (comment) *comment = '\0';

        trim_in_place(line);
        if (line[0] == '\0') continue;

        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (!end) continue;
            *end = '\0';
            strncpy(section, line + 1, sizeof(section) - 1);
            section[sizeof(section) - 1] = '\0';
            trim_in_place(section);
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        trim_in_place(key);
        trim_in_place(val);

        if (strcmp(section, "dependencies") == 0) {
            Dependency dep;
            memset(&dep, 0, sizeof(dep));
            dep.name = strdup(key);
            if (parse_inline_table(val, &dep) == 0 && dep.git_url) {
                if (!dep.version) {
                    dep.version = strdup("main");
                }
                if (!dep.version_kind) {
                    dep.version_kind = strdup("branch");
                }
                dep_list_add(&cfg->dependencies, &dep);
            } else {
                fprintf(stderr, "warning: invalid dependency '%s'\n", key);
                free(dep.name);
                free(dep.git_url);
                free(dep.version);
                free(dep.version_kind);
            }
            continue;
        }

        size_t vlen = strlen(val);
        if (vlen < 2 || val[0] != '"' || val[vlen - 1] != '"') {
            continue;
        }
        val[vlen - 1] = '\0';
        val++;

        if (strcmp(section, "project") == 0) {
            if (strcmp(key, "name") == 0) {
                free(cfg->name);
                cfg->name = strdup(val);
            } else if (strcmp(key, "version") == 0) {
                free(cfg->version);
                cfg->version = strdup(val);
            } else if (strcmp(key, "entry") == 0) {
                free(cfg->entry);
                cfg->entry = strdup(val);
            }
        } else if (strcmp(section, "output") == 0) {
            if (strcmp(key, "path") == 0) {
                free(cfg->output_path);
                cfg->output_path = strdup(val);
            }
        } else if (strcmp(section, "build") == 0) {
            if (strcmp(key, "flags") == 0) {
                free(cfg->build_flags);
                cfg->build_flags = strdup(val);
            }
        }
    }
    fclose(f);
}

static int load_project_config(ProjectConfig* cfg, const char* explicit_path)
{
    memset(cfg, 0, sizeof(*cfg));

    char* config_path = NULL;
    char* config_dir = NULL;

    if (explicit_path) {
        FILE* f = fopen(explicit_path, "r");
        if (!f) {
            fprintf(stderr, "error: cannot open config '%s'\n", explicit_path);
            return -1;
        }
        fclose(f);
        config_path = strdup(explicit_path);

        const char* sep1 = strrchr(explicit_path, '/');
        const char* sep2 = strrchr(explicit_path, '\\');
        const char* sep = (sep1 > sep2) ? sep1 : sep2;
        if (sep) {
            size_t dlen = (size_t)(sep - explicit_path);
            config_dir = malloc(dlen + 1);
            if (!config_dir) OUT_OF_MEMORY();
            memcpy(config_dir, explicit_path, dlen);
            config_dir[dlen] = '\0';
        } else {
            config_dir = strdup(".");
        }
    } else {
        config_path = find_config_file(&config_dir);
        if (!config_path) {
            return 0;
        }
    }

    parse_config(config_path, cfg);
    cfg->config_dir = config_dir;
    cfg->config_path = strdup(config_path);
    cfg->loaded = true;
    free(config_path);
    return 0;
}

static void free_project_config(ProjectConfig* cfg)
{
    free(cfg->name);
    free(cfg->version);
    free(cfg->entry);
    free(cfg->output_path);
    free(cfg->config_dir);
    free(cfg->config_path);
    free(cfg->build_flags);
    free_dependency_list(&cfg->dependencies);
    free_resolved_deps(cfg->resolved);
    memset(cfg, 0, sizeof(*cfg));
}

static int cmd_compile_common(int argc, char** argv, bool force_run)
{
    ProjectConfig cfg;
    if (load_project_config(&cfg, NULL) != 0) {
        free_project_config(&cfg);
        return 1;
    }

    if (cfg.loaded && cfg.dependencies.count > 0) {
        if (resolve_dependencies(&cfg) != 0) {
            free_project_config(&cfg);
            return 1;
        }
    }

    int merged_argc = argc;
    char** merged_argv = argv;
    int extra_flags = 0;

    if (cfg.loaded && cfg.build_flags && cfg.build_flags[0]) {
        extra_flags = build_merged_argv(argc, argv, cfg.build_flags,
                                        &merged_argc, &merged_argv);
        argc = merged_argc;
        argv = merged_argv;
    }

    BuildOptions opts;
    if (parse_build_args(argc, argv, &opts) != 0) {
        for (int i = 1; i <= extra_flags; i++) free(merged_argv[i]);
        if (extra_flags > 0) free(merged_argv);
        free_build_options(&opts);
        free_project_config(&cfg);
        return 1;
    }
    if (force_run) opts.run = true;

    if (opts.output == NULL) {
        if (cfg.loaded && cfg.output_path) {
            opts.output = path_join(cfg.config_dir, cfg.output_path);
        } else {
            opts.output = strdup("output.exe");
        }
        if (!opts.output) OUT_OF_MEMORY();
    }

    expand_inputs(&opts);

    if (opts.file_count == 0) {
        if (cfg.loaded) {
            char* src_dir = path_join(cfg.config_dir, "src");
            if (is_directory(src_dir)) {
                scan_dir_recursive(&opts, src_dir);
            }
            free(src_dir);
        }

        if (opts.file_count == 0 && is_directory("src")) {
            scan_dir_recursive(&opts, "src");
        }
    }

    if (opts.file_count == 0 && cfg.loaded && cfg.entry) {
        char* entry_path = path_join(cfg.config_dir, cfg.entry);
        if (is_directory(entry_path)) {
            scan_dir_recursive(&opts, entry_path);
        } else {
            add_input_file(&opts, entry_path);
        }
        free(entry_path);
    }

    if (opts.file_count == 0) {
        fprintf(stderr, "error: no input files\n");
        fprintf(stderr, "usage: yxg build [options] <file.yxg | dir>...\n");
        free_build_options(&opts);
        free_project_config(&cfg);
        return 1;
    }

    if (cfg.loaded && cfg.resolved) {
        collect_dependency_sources(&opts, &cfg);
    }

    sort_inputs(&opts);

    if (!opts.force_rebuild && !needs_rebuild(&opts, &cfg)) {
        if (opts.run) {
            char run_cmd[MAX_PATH * 2];
            snprintf(run_cmd, sizeof(run_cmd), "\"%s\"", opts.output);
            int run_ret = 0;
            FILE* probe2 = fopen(opts.output, "rb");
            if (!probe2) {
                fprintf(stderr, "error: executable not found: %s\n", opts.output);
                run_ret = 1;
            } else {
                fclose(probe2);
                fflush(stdout);
                fflush(stderr);
                fprintf(stderr, "run: %s\n", opts.output);
                fflush(stderr);
                run_ret = system(run_cmd);
                if (run_ret != 0) {
                    unsigned int rc2 = (unsigned int)run_ret;
                    fprintf(stderr, "run: exit code %d (0x%08X)\n", run_ret, rc2);
                    if (rc2 >= 0x80000000u) {
                        fprintf(stderr, "run: program crashed (0x%08X is usually an access violation)\n", rc2);
                        fprintf(stderr, "     try: yxg build <file>, then run the exe by hand\n");
                    }
                } else {
                    fprintf(stderr, "run: exit code 0\n");
                }
            }
            free_build_options(&opts);
            free_project_config(&cfg);
            return run_ret;
        }
        fprintf(stderr, "up to date\n");
        free_build_options(&opts);
        free_project_config(&cfg);
        return 0;
    }

    int ret = run_compiler(&opts);
    free_build_options(&opts);
    free_project_config(&cfg);
    if (extra_flags > 0) {
        for (int i = 1; i <= extra_flags; i++) free(merged_argv[i]);
        free(merged_argv);
    }
    return ret;
}

static void dep_list_add(DependencyList* list, Dependency* dep)
{
    if (list->count >= list->capacity) {
        int nc = list->capacity ? list->capacity * 2 : 4;
        Dependency* ni = realloc(list->items, sizeof(Dependency) * nc);
        if (!ni) OUT_OF_MEMORY();
        list->items = ni;
        list->capacity = nc;
    }
    list->items[list->count++] = *dep;
}

static void free_dependency_list(DependencyList* list)
{
    for (int i = 0; i < list->count; i++) {
        free(list->items[i].name);
        free(list->items[i].git_url);
        free(list->items[i].version);
        free(list->items[i].version_kind);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void free_resolved_deps(ResolvedDep* d)
{
    while (d) {
        ResolvedDep* next = d->next;
        free(d->name);
        free(d->path);
        free(d);
        d = next;
    }
}

static int parse_inline_table(char* s, Dependency* dep)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '{') return -1;
    s++;

    char* end = strrchr(s, '}');
    if (!end) return -1;
    *end = '\0';

#ifdef _WIN32
    char* saveptr = NULL;
    char* token = strtok_s(s, ",", &saveptr);
#else
    char* saveptr = NULL;
    char* token = strtok_r(s, ",", &saveptr);
#endif

    while (token) {
        while (*token == ' ' || *token == '\t') token++;
        size_t tlen = strlen(token);
        while (tlen > 0 && (token[tlen - 1] == ' ' || token[tlen - 1] == '\t')) {
            token[--tlen] = '\0';
        }

        char* eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char* k = token;
            char* v = eq + 1;

            size_t klen = strlen(k);
            while (klen > 0 && (k[klen - 1] == ' ' || k[klen - 1] == '\t')) {
                k[--klen] = '\0';
            }
            while (*v == ' ' || *v == '\t') v++;

            size_t vlen = strlen(v);
            if (vlen >= 2 && v[0] == '"' && v[vlen - 1] == '"') {
                v[vlen - 1] = '\0';
                v++;
            }

            if (strcmp(k, "git") == 0) {
                free(dep->git_url);
                dep->git_url = strdup(v);
            } else if (strcmp(k, "tag") == 0) {
                free(dep->version);
                dep->version = strdup(v);
                free(dep->version_kind);
                dep->version_kind = strdup("tag");
            } else if (strcmp(k, "branch") == 0) {
                free(dep->version);
                dep->version = strdup(v);
                free(dep->version_kind);
                dep->version_kind = strdup("branch");
            } else if (strcmp(k, "commit") == 0) {
                free(dep->version);
                dep->version = strdup(v);
                free(dep->version_kind);
                dep->version_kind = strdup("commit");
            } else if (strcmp(k, "path") == 0) {
                free(dep->git_url);
                dep->git_url = strdup(v);
                free(dep->version_kind);
                dep->version_kind = strdup("path");
            }
        }

#ifdef _WIN32
        token = strtok_s(NULL, ",", &saveptr);
#else
        token = strtok_r(NULL, ",", &saveptr);
#endif
    }
    return 0;
}

static void get_cache_dir(char* buf, size_t bufsize)
{
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    if (!home) home = ".";

#ifdef _WIN32
    snprintf(buf, bufsize, "%s\\.yxg\\cache", home);
#else
    snprintf(buf, bufsize, "%s/.yxg/cache", home);
#endif
}

static int download_dependency(Dependency* dep)
{
    if (strcmp(dep->version_kind, "path") == 0) {
        return 0;
    }

    char cache_dir[YXG_PATH_MAX];
    get_cache_dir(cache_dir, sizeof(cache_dir));

    char parent[YXG_PATH_MAX];
#ifdef _WIN32
    snprintf(parent, sizeof(parent), "%s\\.yxg", cache_dir);
#else
    {
        char* last = strrchr(cache_dir, '/');
        if (last) {
            size_t len = (size_t)(last - cache_dir);
            memcpy(parent, cache_dir, len);
            parent[len] = '\0';
        } else {
            strcpy(parent, ".");
        }
    }
#endif
    MAKE_DIR(parent);
    MAKE_DIR(cache_dir);

    char target[YXG_PATH_MAX];
#ifdef _WIN32
    snprintf(target, sizeof(target), "%s\\%s@%s",
             cache_dir, dep->name, dep->version);
#else
    snprintf(target, sizeof(target), "%s/%s@%s",
             cache_dir, dep->name, dep->version);
#endif

    if (is_directory(target)) {
        return 0;
    }

    char cmd[4096];

    if (strcmp(dep->version_kind, "commit") == 0) {
        snprintf(cmd, sizeof(cmd),
                 "git clone \"%s\" \"%s\"",
                 dep->git_url, target);
        fprintf(stderr, "  cloning %s (full)\n", dep->git_url);
        if (system(cmd) != 0) {
            fprintf(stderr, "error: git clone failed for '%s'\n", dep->name);
            return -1;
        }

        char cmd2[4096];
        snprintf(cmd2, sizeof(cmd2),
                 "git -C \"%s\" checkout %s",
                 target, dep->version);
        if (system(cmd2) != 0) {
            fprintf(stderr, "error: git checkout failed for '%s'\n", dep->name);
            return -1;
        }
    } else {
        snprintf(cmd, sizeof(cmd),
                 "git clone --depth 1 --branch %s \"%s\" \"%s\"",
                 dep->version, dep->git_url, target);
        fprintf(stderr, "  fetching %s@%s\n", dep->name, dep->version);
        if (system(cmd) != 0) {
            fprintf(stderr, "error: git clone failed for '%s'\n", dep->name);
            return -1;
        }
    }

    return 0;
}

static void resolve_dependencies_recursive(DependencyList* deps, ResolvedDep** out, int depth)
{
    if (depth > 32) {
        fprintf(stderr, "warning: dependency depth exceeds 32, stopping\n");
        return;
    }

    for (int i = 0; i < deps->count; i++) {
        Dependency* dep = &deps->items[i];

        bool already = false;
        for (ResolvedDep* d = *out; d; d = d->next) {
            if (strcmp(d->name, dep->name) == 0) {
                already = true;
                break;
            }
        }
        if (already) continue;

        if (download_dependency(dep) != 0) {
            continue;
        }

        char local_path[YXG_PATH_MAX];
        if (strcmp(dep->version_kind, "path") == 0) {
            snprintf(local_path, sizeof(local_path), "%s", dep->git_url);
        } else {
            char cache_dir[YXG_PATH_MAX];
            get_cache_dir(cache_dir, sizeof(cache_dir));
#ifdef _WIN32
            snprintf(local_path, sizeof(local_path), "%s\\%s@%s",
                     cache_dir, dep->name, dep->version);
#else
            snprintf(local_path, sizeof(local_path), "%s/%s@%s",
                     cache_dir, dep->name, dep->version);
#endif
        }

        ResolvedDep* node = calloc(1, sizeof(ResolvedDep));
        if (!node) OUT_OF_MEMORY();
        node->name = strdup(dep->name);
        node->path = strdup(local_path);
        node->next = *out;
        *out = node;

        char sub_config[YXG_PATH_MAX];
#ifdef _WIN32
        snprintf(sub_config, sizeof(sub_config), "%s\\yxg.toml", local_path);
#else
        snprintf(sub_config, sizeof(sub_config), "%s/yxg.toml", local_path);
#endif

        FILE* f = fopen(sub_config, "r");
        if (!f) continue;
        fclose(f);

        ProjectConfig sub_cfg;
        memset(&sub_cfg, 0, sizeof(sub_cfg));
        parse_config(sub_config, &sub_cfg);
        if (sub_cfg.dependencies.count > 0) {
            resolve_dependencies_recursive(&sub_cfg.dependencies, out, depth + 1);
        }
        free_project_config(&sub_cfg);
    }
}

static int resolve_dependencies(ProjectConfig* cfg)
{
    resolve_dependencies_recursive(&cfg->dependencies, &cfg->resolved, 0);
    return 0;
}

static void collect_dependency_sources(BuildOptions* opts, ProjectConfig* cfg)
{
    for (ResolvedDep* d = cfg->resolved; d; d = d->next) {
        char src_dir[YXG_PATH_MAX];
#ifdef _WIN32
        snprintf(src_dir, sizeof(src_dir), "%s\\src", d->path);
#else
        snprintf(src_dir, sizeof(src_dir), "%s/src", d->path);
#endif

        if (is_directory(src_dir)) {
            scan_dir_recursive(opts, src_dir);
        }

        module_path_add(d->path);
    }
}

static int cmd_clean(void)
{
    const char* tmpfiles[] = {
        "output.asm", "output.obj",
        "image_loader.obj",
        "runtime_music.obj", "music_loader.obj",
        "runtime_video.obj", "video_loader.obj",
        "yxg_app.o", "yxg_app.rc", "yxg_app.manifest"
    };
    int count = 0;
    for (size_t i = 0; i < sizeof(tmpfiles) / sizeof(tmpfiles[0]); i++) {
        if (remove(tmpfiles[i]) == 0) count++;
    }

    char cache_dir[MAX_PATH * 2];
    char exe_dir[MAX_PATH] = "";
#ifdef _WIN32
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char* last_sep = strrchr(exe_dir, '\\');
    if (last_sep) *last_sep = '\0';
#else
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len != -1) {
        exe_dir[len] = '\0';
        char* last_sep = strrchr(exe_dir, '/');
        if (last_sep) *last_sep = '\0';
    }
#endif

    if (exe_dir[0] != '\0') {
        snprintf(cache_dir, sizeof(cache_dir), "%s%cruntime_cache", exe_dir, '\\');

        const char* runtime_names[] = {
            "runtime_print", "runtime_gc", "runtime_unsafe", "runtime_int64",
            "runtime_string", "runtime_memory", "runtime_float", "runtime_array",
            "runtime_system", "runtime_bounds", "runtime_exception",
            "runtime_file", "runtime_socket", "runtime_window", "runtime_time"
        };
        for (size_t i = 0; i < sizeof(runtime_names) / sizeof(runtime_names[0]); i++) {
            char obj_path[MAX_PATH * 2];
            snprintf(obj_path, sizeof(obj_path), "%s\\%s.obj", cache_dir, runtime_names[i]);
            if (remove(obj_path) == 0) count++;
        }

        if (is_directory(cache_dir)) {
            remove(cache_dir);
        }
    }

    printf("cleaned %d file(s).\n", count);
    return 0;
}

static int proj_mkdir(const char* path)
{
    MAKE_DIR(path);
    return 0;
}

static int proj_write(const char* path, const char* content)
{
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "error: cannot create '%s'\n", path);
        return -1;
    }
    fputs(content, f);
    fclose(f);
    return 0;
}

static int proj_write_common(const char* name)
{
    char path[1024];
    char body[2048];

    snprintf(path, sizeof(path), "%s/yxg.toml", name);
    snprintf(body, sizeof(body),
        "[project]\n"
        "name = \"%s\"\n"
        "version = \"0.1.0\"\n"
        "entry = \"src/Main.yxg\"\n"
        "\n"
        "[output]\n"
        "path = \"bin/%s.exe\"\n"
        "\n"
        "[build]\n"
        "# 编译时附加参数，等同命令行效果，命令行优先级更高\n"
        "# 支持: -a -w -r -noassert --force -o <path>\n"
        "flags = \"\"\n", name, name);
    if (proj_write(path, body) != 0) return -1;

    snprintf(path, sizeof(path), "%s/.gitignore", name);
    if (proj_write(path,
        "bin/\n"
        "*.obj\n"
        "*.asm\n"
        "runtime_cache/\n"
        ".vscode/\n"
        ".idea/\n"
        "*.swp\n") != 0) return -1;

    return 0;
}

static int proj_gen_console(const char* name)
{
    char path[1024];
    char body[4096];

    snprintf(path, sizeof(path), "%s/src", name);
    proj_mkdir(path);

    snprintf(path, sizeof(path), "%s/src/Main.yxg", name);
    snprintf(body, sizeof(body),
        "pub void main() {\n"
        "    print \"hello, %s\\n\";\n"
        "}\n", name);
    if (proj_write(path, body) != 0) return -1;

    snprintf(path, sizeof(path), "%s/README.md", name);
    snprintf(body, sizeof(body),
        "# %s\n"
        "\n"
        "A YXG console application.\n"
        "\n"
        "## Build\n"
        "\n"
        "```\n"
        "yxg build\n"
        "```\n"
        "\n"
        "## Run\n"
        "\n"
        "```\n"
        "yxg run\n"
        "```\n", name);
    if (proj_write(path, body) != 0) return -1;

    return proj_write_common(name);
}

static int proj_gen_game(const char* name)
{
    char path[1024];
    char body[8192];

    snprintf(path, sizeof(path), "%s/src", name);
    proj_mkdir(path);

    snprintf(path, sizeof(path), "%s/assets", name);
    proj_mkdir(path);

    snprintf(path, sizeof(path), "%s/src/Main.yxg", name);
    snprintf(body, sizeof(body),
        "import yxg.Window;\n"
        "\n"
        "class Game {\n"
        "    Window win;\n"
        "    int running;\n"
        "\n"
        "    pub Game() {\n"
        "        win = new Window();\n"
        "        running = 1;\n"
        "    }\n"
        "\n"
        "    pub void init() {\n"
        "        win.create(\"%s\", 800, 600);\n"
        "    }\n"
        "\n"
        "    pub void update(int dt_ms) {\n"
        "        if win.is_key_pressed(27) == 1 {\n"
        "            running = 0;\n"
        "        }\n"
        "    }\n"
        "\n"
        "    pub void render() {\n"
        "        win.clear(0x1A1A2E);\n"
        "        win.draw_text(20, 20, \"hello, %s\", 0xFFFFFF);\n"
        "        win.flush();\n"
        "    }\n"
        "}\n"
        "\n"
        "pub void main() {\n"
        "    Game g = new Game();\n"
        "    g.init();\n"
        "    while g.running == 1 {\n"
        "        if g.win.update() == 0 { break; }\n"
        "        g.update(16);\n"
        "        g.render();\n"
        "    }\n"
        "    g.win.close();\n"
        "}\n", name, name);
    if (proj_write(path, body) != 0) return -1;

    snprintf(path, sizeof(path), "%s/README.md", name);
    snprintf(body, sizeof(body),
        "# %s\n"
        "\n"
        "A YXG game project built on the Window library.\n"
        "\n"
        "## Build\n"
        "\n"
        "```\n"
        "yxg build -w\n"
        "```\n"
        "\n"
        "## Run\n"
        "\n"
        "```\n"
        "yxg run\n"
        "```\n", name);
    if (proj_write(path, body) != 0) return -1;

    return proj_write_common(name);
}

static int proj_gen_lib(const char* name)
{
    char path[1024];
    char body[4096];

    snprintf(path, sizeof(path), "%s/src", name);
    proj_mkdir(path);

    snprintf(path, sizeof(path), "%s/tests", name);
    proj_mkdir(path);

    snprintf(path, sizeof(path), "%s/src/%s.yxg", name, name);
    snprintf(body, sizeof(body),
        "pub class %s {\n"
        "    pub static int add(int a, int b) {\n"
        "        return a + b;\n"
        "    }\n"
        "}\n", name);
    if (proj_write(path, body) != 0) return -1;

    snprintf(path, sizeof(path), "%s/src/Main.yxg", name);
    snprintf(body, sizeof(body),
        "import %s;\n"
        "\n"
        "pub void main() {\n"
        "    print %s.add(1, 2), \"\\n\";\n"
        "}\n", name, name);
    if (proj_write(path, body) != 0) return -1;

    snprintf(path, sizeof(path), "%s/tests/Test.yxg", name);
    snprintf(body, sizeof(body),
        "import %s;\n"
        "\n"
        "pub void test_add() {\n"
        "    int r = %s.add(2, 3);\n"
        "    if r != 5 {\n"
        "        throw new ERROR(\"test_add failed\");\n"
        "    }\n"
        "}\n", name, name);
    if (proj_write(path, body) != 0) return -1;

    snprintf(path, sizeof(path), "%s/README.md", name);
    snprintf(body, sizeof(body),
        "# %s\n"
        "\n"
        "A reusable YXG library.\n"
        "\n"
        "## Usage\n"
        "\n"
        "```yxg\n"
        "import %s;\n"
        "\n"
        "int sum = %s.add(1, 2);\n"
        "```\n"
        "\n"
        "## Test\n"
        "\n"
        "```\n"
        "yxg run tests/Test.yxg\n"
        "```\n", name, name, name);
    if (proj_write(path, body) != 0) return -1;

    return proj_write_common(name);
}

static void cmd_new_help(void)
{
    printf("usage: yxg new <project-name> [-t console|game|lib]\n");
    printf("\n");
    printf("Templates:\n");
    printf("  console   Hello-world command line program (default)\n");
    printf("  game      Window + game loop skeleton\n");
    printf("  lib       Reusable library with tests\n");
}

static int cmd_new(int argc, char** argv)
{
    const char* name = NULL;
    const char* template_str = "console";
    bool show_help = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help = true;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--template") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -t requires an argument\n");
                return 1;
            }
            template_str = argv[++i];
        } else if (argv[i][0] != '-') {
            if (name != NULL) {
                fprintf(stderr, "error: multiple project names given\n");
                return 1;
            }
            name = argv[i];
        } else {
            fprintf(stderr, "warning: unknown option '%s'\n", argv[i]);
        }
    }

    if (show_help) {
        cmd_new_help();
        return 0;
    }

    if (name == NULL) {
        fprintf(stderr, "error: no project name given\n");
        cmd_new_help();
        return 1;
    }

    if (MAKE_DIR(name) != 0) {
        fprintf(stderr, "error: cannot create directory '%s' (already exists?)\n", name);
        return 1;
    }

    int rc;
    if (strcmp(template_str, "console") == 0) {
        rc = proj_gen_console(name);
    } else if (strcmp(template_str, "game") == 0) {
        rc = proj_gen_game(name);
    } else if (strcmp(template_str, "lib") == 0) {
        rc = proj_gen_lib(name);
    } else {
        fprintf(stderr, "error: unknown template '%s'\n", template_str);
        fprintf(stderr, "available templates: console, game, lib\n");
        return 1;
    }

    if (rc != 0) {
        fprintf(stderr, "error: template generation failed, partial project left at '%s'\n", name);
        return 1;
    }

    printf("created project '%s' (%s template)\n", name, template_str);
    return 0;
}

int main(int argc, char** argv)
{
    error_init();

    if (argc < 2) {
        print_help();
        return 0;
    }

    if (strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "help") == 0) {
        print_help();
        return 0;
    }
    if (strcmp(argv[1], "-version") == 0) {
        printf("version: 2.0.0\n");
        return 0;
    }
    if (strcmp(argv[1], "-time") == 0) {
        time_t current_timestamp;
        char time_str[100];
        time(&current_timestamp);
        strftime(time_str, sizeof(time_str), "Current time: %Y-%m-%d %H:%M:%S", localtime(&current_timestamp));
        puts(time_str);
        return 0;
    }

    if (strcmp(argv[1], "build") == 0) return cmd_build(argc - 1, argv + 1);
    if (strcmp(argv[1], "run") == 0) return cmd_run(argc - 1, argv + 1);
    if (strcmp(argv[1], "clean") == 0) return cmd_clean();
    if (strcmp(argv[1], "new") == 0) return cmd_new(argc - 1, argv + 1);

    return cmd_build(argc, argv);
}

