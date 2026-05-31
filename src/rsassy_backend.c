#define _GNU_SOURCE

#include <R.h>

#include "rsassy_native.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#include <unistd.h>
#endif

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#endif

#define RSASSY_ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

static const char *const RSASSY_BACKENDS[] = {
    "scalar",
    "neon",
    "avx2",
    "avx512",
    "wasm_simd128",
};

static const char *const RSASSY_BACKEND_PRIORITY[] = {
    "wasm_simd128",
    "avx512",
    "avx2",
    "neon",
    "scalar",
};

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
typedef HMODULE RsassyBackendHandle;
#else
typedef void *RsassyBackendHandle;
#endif

Rsassy_searcher_new_fn Rsassy_rsassy_searcher_new = NULL;
Rsassy_searcher_free_fn Rsassy_rsassy_searcher_free = NULL;
Rsassy_searcher_search_fn Rsassy_rsassy_searcher_search = NULL;
Rsassy_searcher_search_many_fn Rsassy_rsassy_searcher_search_many = NULL;
Rsassy_crispr_search_many_fn Rsassy_rsassy_crispr_search_many = NULL;
Rsassy_matches_free_fn Rsassy_rsassy_matches_free = NULL;
Rsassy_fastx_iter_new_fn Rsassy_rsassy_fastx_iter_new = NULL;
Rsassy_fastx_iter_free_fn Rsassy_rsassy_fastx_iter_free = NULL;
Rsassy_fastx_iter_next_fn Rsassy_rsassy_fastx_iter_next = NULL;
Rsassy_fastx_batch_free_fn Rsassy_rsassy_fastx_batch_free = NULL;
Rsassy_fastx_batch_n_fn Rsassy_rsassy_fastx_batch_n = NULL;
Rsassy_fastx_batch_has_qual_fn Rsassy_rsassy_fastx_batch_has_qual = NULL;
Rsassy_fastx_batch_slice_ptr_fn Rsassy_rsassy_fastx_batch_id_ptr = NULL;
Rsassy_fastx_batch_slice_len_fn Rsassy_rsassy_fastx_batch_id_len = NULL;
Rsassy_fastx_batch_index_bool_fn Rsassy_rsassy_fastx_batch_id_utf8 = NULL;
Rsassy_fastx_batch_slice_ptr_fn Rsassy_rsassy_fastx_batch_seq_ptr = NULL;
Rsassy_fastx_batch_slice_len_fn Rsassy_rsassy_fastx_batch_seq_len = NULL;
Rsassy_fastx_batch_slice_ptr_fn Rsassy_rsassy_fastx_batch_qual_ptr = NULL;
Rsassy_fastx_batch_slice_len_fn Rsassy_rsassy_fastx_batch_qual_len = NULL;
Rsassy_last_error_message_fn Rsassy_rsassy_last_error_message = NULL;
Rsassy_features_string_fn Rsassy_rsassy_features_string = NULL;
#endif

static int Rsassy_backend_loaded = 0;
static char Rsassy_requested_backend[32] = "";
static char Rsassy_selected_backend[32] = "";
static char Rsassy_available_backends_buf[128] = "";

/* Backend names are the public C/R dispatch names.  Keep this list small and
 * explicit; do not accept arbitrary filenames or Rust feature strings here. */
static int Rsassy_known_backend(const char *backend) {
    if (backend == NULL) {
        return 0;
    }
    for (size_t i = 0; i < RSASSY_ARRAY_LEN(RSASSY_BACKENDS); i++) {
        if (strcmp(backend, RSASSY_BACKENDS[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int Rsassy_csv_has(const char *csv, const char *name) {
    size_t name_len = strlen(name);
    const char *p = csv == NULL ? "" : csv;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == ',') {
            p++;
        }
        const char *start = p;
        while (*p != '\0' && *p != ',') {
            p++;
        }
        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        if ((size_t)(end - start) == name_len && strncmp(start, name, name_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ---- Runtime CPU feature checks -----------------------------------------
 *
 * Rsassy builds each native Rust backend with a target CPU level.  The loader
 * therefore needs to check the matching *runtime* CPU and OS state before it
 * ever dlopens/LoadLibrary's a backend.  We intentionally avoid compiler
 * builtins here: Apple clang rejects some feature-string spellings that GCC and
 * LLVM otherwise accept, so direct CPUID/XGETBV keeps the check portable.
 */

#if defined(__x86_64__) || defined(_M_X64)
typedef struct RsassyCpuRegs {
    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;
} RsassyCpuRegs;

enum {
    RSASSY_LEAF_1 = 1u,
    RSASSY_LEAF_7 = 7u,
    RSASSY_EXT_LEAF_1 = 0x80000001u,
};

enum {
    RSASSY_ECX_SSE3 = 0,
    RSASSY_ECX_SSSE3 = 9,
    RSASSY_ECX_FMA = 12,
    RSASSY_ECX_SSE41 = 19,
    RSASSY_ECX_SSE42 = 20,
    RSASSY_ECX_MOVBE = 22,
    RSASSY_ECX_POPCNT = 23,
    RSASSY_ECX_XSAVE = 26,
    RSASSY_ECX_OSXSAVE = 27,
    RSASSY_ECX_AVX = 28,
    RSASSY_ECX_F16C = 29,
    RSASSY_EXT_ECX_LZCNT = 5,
};

enum {
    RSASSY_EBX_BMI1 = 3,
    RSASSY_EBX_AVX2 = 5,
    RSASSY_EBX_BMI2 = 8,
    RSASSY_EBX_AVX512F = 16,
    RSASSY_EBX_AVX512DQ = 17,
    RSASSY_EBX_AVX512CD = 28,
    RSASSY_EBX_AVX512BW = 30,
    RSASSY_EBX_AVX512VL = 31,
};

static RsassyCpuRegs Rsassy_cpuid(unsigned int leaf, unsigned int subleaf) {
    RsassyCpuRegs out;
#if defined(_MSC_VER)
    int regs[4];
    __cpuidex(regs, (int)leaf, (int)subleaf);
    out.eax = (unsigned int)regs[0];
    out.ebx = (unsigned int)regs[1];
    out.ecx = (unsigned int)regs[2];
    out.edx = (unsigned int)regs[3];
#else
    __asm__ volatile("cpuid"
                     : "=a"(out.eax), "=b"(out.ebx), "=c"(out.ecx), "=d"(out.edx)
                     : "a"(leaf), "c"(subleaf));
#endif
    return out;
}

static uint64_t Rsassy_xgetbv(unsigned int index) {
#if defined(_MSC_VER)
    return (uint64_t)_xgetbv(index);
#else
    unsigned int eax;
    unsigned int edx;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
#endif
}

static int Rsassy_bit(unsigned int value, unsigned int bit) {
    return (value & (1u << bit)) != 0;
}

static int Rsassy_has_leaf(unsigned int leaf) {
    return Rsassy_cpuid(0, 0).eax >= leaf;
}

static int Rsassy_has_ext_leaf(unsigned int leaf) {
    return Rsassy_cpuid(0x80000000u, 0).eax >= leaf;
}

static int Rsassy_os_saves_ymm(const RsassyCpuRegs *leaf1) {
    if (!Rsassy_bit(leaf1->ecx, RSASSY_ECX_XSAVE) ||
        !Rsassy_bit(leaf1->ecx, RSASSY_ECX_OSXSAVE)) {
        return 0;
    }
    return (Rsassy_xgetbv(0) & 0x6u) == 0x6u;
}

static int Rsassy_os_saves_zmm(const RsassyCpuRegs *leaf1) {
    if (!Rsassy_os_saves_ymm(leaf1)) {
        return 0;
    }
    return (Rsassy_xgetbv(0) & 0xE6u) == 0xE6u;
}

int Rsassy_cpu_avx2(void) {
    if (!Rsassy_has_leaf(RSASSY_LEAF_7)) {
        return 0;
    }

    RsassyCpuRegs leaf1 = Rsassy_cpuid(RSASSY_LEAF_1, 0);
    if (!Rsassy_os_saves_ymm(&leaf1)) {
        return 0;
    }

    RsassyCpuRegs leaf7 = Rsassy_cpuid(RSASSY_LEAF_7, 0);
    return Rsassy_bit(leaf7.ebx, RSASSY_EBX_AVX2);
}

int Rsassy_cpu_avx512f(void) {
    if (!Rsassy_has_leaf(RSASSY_LEAF_7)) {
        return 0;
    }

    RsassyCpuRegs leaf1 = Rsassy_cpuid(RSASSY_LEAF_1, 0);
    if (!Rsassy_os_saves_zmm(&leaf1)) {
        return 0;
    }

    RsassyCpuRegs leaf7 = Rsassy_cpuid(RSASSY_LEAF_7, 0);
    return Rsassy_bit(leaf7.ebx, RSASSY_EBX_AVX512F);
}

static int Rsassy_cpu_supports_x86_64_v3(void) {
    if (!Rsassy_has_leaf(RSASSY_LEAF_7) || !Rsassy_has_ext_leaf(RSASSY_EXT_LEAF_1)) {
        return 0;
    }

    RsassyCpuRegs leaf1 = Rsassy_cpuid(RSASSY_LEAF_1, 0);
    if (!Rsassy_os_saves_ymm(&leaf1)) {
        return 0;
    }

    RsassyCpuRegs leaf7 = Rsassy_cpuid(RSASSY_LEAF_7, 0);
    RsassyCpuRegs ext1 = Rsassy_cpuid(RSASSY_EXT_LEAF_1, 0);

    return Rsassy_bit(leaf1.ecx, RSASSY_ECX_SSE3) &&
           Rsassy_bit(leaf1.ecx, RSASSY_ECX_SSSE3) &&
           Rsassy_bit(leaf1.ecx, RSASSY_ECX_FMA) &&
           Rsassy_bit(leaf1.ecx, RSASSY_ECX_SSE41) &&
           Rsassy_bit(leaf1.ecx, RSASSY_ECX_SSE42) &&
           Rsassy_bit(leaf1.ecx, RSASSY_ECX_MOVBE) &&
           Rsassy_bit(leaf1.ecx, RSASSY_ECX_POPCNT) &&
           Rsassy_bit(leaf1.ecx, RSASSY_ECX_AVX) &&
           Rsassy_bit(leaf1.ecx, RSASSY_ECX_F16C) &&
           Rsassy_bit(leaf7.ebx, RSASSY_EBX_BMI1) &&
           Rsassy_bit(leaf7.ebx, RSASSY_EBX_AVX2) &&
           Rsassy_bit(leaf7.ebx, RSASSY_EBX_BMI2) &&
           Rsassy_bit(ext1.ecx, RSASSY_EXT_ECX_LZCNT);
}

static int Rsassy_cpu_supports_x86_64_v4(void) {
    if (!Rsassy_cpu_supports_x86_64_v3() || !Rsassy_has_leaf(RSASSY_LEAF_7)) {
        return 0;
    }

    RsassyCpuRegs leaf1 = Rsassy_cpuid(RSASSY_LEAF_1, 0);
    if (!Rsassy_os_saves_zmm(&leaf1)) {
        return 0;
    }

    RsassyCpuRegs leaf7 = Rsassy_cpuid(RSASSY_LEAF_7, 0);
    return Rsassy_bit(leaf7.ebx, RSASSY_EBX_AVX512F) &&
           Rsassy_bit(leaf7.ebx, RSASSY_EBX_AVX512DQ) &&
           Rsassy_bit(leaf7.ebx, RSASSY_EBX_AVX512CD) &&
           Rsassy_bit(leaf7.ebx, RSASSY_EBX_AVX512BW) &&
           Rsassy_bit(leaf7.ebx, RSASSY_EBX_AVX512VL);
}
#else
int Rsassy_cpu_avx2(void) {
    return 0;
}

int Rsassy_cpu_avx512f(void) {
    return 0;
}

static int Rsassy_cpu_supports_x86_64_v3(void) {
    return 0;
}

static int Rsassy_cpu_supports_x86_64_v4(void) {
    return 0;
}
#endif

int Rsassy_cpu_neon(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
    /* AArch64 has Advanced SIMD/NEON as part of the baseline architecture. */
    return 1;
#else
    return 0;
#endif
}

bool rsassy_backend_supported(const char *name) {
    if (name == NULL) {
        return false;
    }
    if (strcmp(name, "scalar") == 0) {
        return true;
    }
    if (strcmp(name, "avx2") == 0) {
        return Rsassy_cpu_supports_x86_64_v3() != 0;
    }
    if (strcmp(name, "avx512") == 0) {
        return Rsassy_cpu_supports_x86_64_v4() != 0;
    }
    if (strcmp(name, "neon") == 0) {
        return Rsassy_cpu_neon() != 0;
    }
    if (strcmp(name, "wasm_simd128") == 0) {
#ifdef __EMSCRIPTEN__
        return true;
#else
        return false;
#endif
    }
    return false;
}

const char *rsassy_select_backend(const char *available) {
    for (size_t i = 0; i < RSASSY_ARRAY_LEN(RSASSY_BACKEND_PRIORITY); i++) {
        const char *candidate = RSASSY_BACKEND_PRIORITY[i];
        if (Rsassy_csv_has(available, candidate) && rsassy_backend_supported(candidate)) {
            return candidate;
        }
    }
    return "";
}

int Rsassy_backend_is_loaded(void) {
    return Rsassy_backend_loaded;
}

const char *Rsassy_requested_backend_name(void) {
    return Rsassy_requested_backend[0] == '\0' ? "auto" : Rsassy_requested_backend;
}

const char *Rsassy_selected_backend_name(void) {
    return Rsassy_selected_backend[0] == '\0' ? "unknown" : Rsassy_selected_backend;
}

const char *Rsassy_available_backend_names(void) {
    return Rsassy_available_backends_buf[0] == '\0' ? "none" : Rsassy_available_backends_buf;
}

const char *Rsassy_dispatch_mode(void) {
#ifdef __EMSCRIPTEN__
    return "static";
#else
    return "dynamic";
#endif
}

static int Rsassy_backend_in_this_build(const char *backend) {
#ifdef __EMSCRIPTEN__
    return strcmp(backend, "wasm_simd128") == 0;
#else
    return strcmp(backend, "wasm_simd128") != 0;
#endif
}

void Rsassy_request_backend(const char *backend) {
    if (Rsassy_backend_loaded) {
        Rf_error("Rsassy backend is already initialized; call sassy_set_backend() before sassy_features(), sassy_searcher(), or sassy_search()");
    }
    if (backend == NULL || strcmp(backend, "auto") == 0) {
        Rsassy_requested_backend[0] = '\0';
        return;
    }
    if (!Rsassy_known_backend(backend)) {
        Rf_error("unknown Rsassy backend '%s'", backend);
    }
    if (!Rsassy_backend_in_this_build(backend)) {
        Rf_error("requested Rsassy backend '%s' is not installed in this %s build", backend, Rsassy_dispatch_mode());
    }
    if (!rsassy_backend_supported(backend)) {
        Rf_error("requested Rsassy backend '%s' is not supported on this CPU/runtime", backend);
    }
    snprintf(Rsassy_requested_backend, sizeof(Rsassy_requested_backend), "%s", backend);
}

#ifndef __EMSCRIPTEN__
static int Rsassy_file_exists(const char *path) {
#ifdef _WIN32
    return _access(path, 4) == 0;
#else
    return access(path, R_OK) == 0;
#endif
}

static char *Rsassy_last_path_separator(char *path) {
    char *slash = strrchr(path, '/');
#ifdef _WIN32
    char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
#endif
    return slash;
}

/* Convert the loaded R library directory to the sibling backend directory:
 *   libs      -> backends
 *   libs/x64  -> backends/x64
 */
static void Rsassy_use_backend_dir(char *out, size_t out_size) {
    char *slash = Rsassy_last_path_separator(out);
    char *base = slash == NULL ? out : slash + 1;

    if (strcmp(base, "libs") == 0) {
        size_t prefix_len = (size_t)(base - out);
        size_t need = prefix_len + strlen("backends") + 1;
        if (need > out_size) {
            Rf_error("Rsassy backend path is too long");
        }
        strcpy(base, "backends");
        return;
    }

    if (slash == NULL) {
        Rf_error("failed to derive Rsassy backend directory from shared library path");
    }

    char sep = *slash;
    char arch[128];
    if (strlen(base) >= sizeof(arch)) {
        Rf_error("Rsassy architecture directory name is too long");
    }
    strcpy(arch, base);
    *slash = '\0';

    char *parent_slash = Rsassy_last_path_separator(out);
    char *parent_base = parent_slash == NULL ? out : parent_slash + 1;
    if (strcmp(parent_base, "libs") != 0) {
        Rf_error("failed to derive Rsassy backend directory from shared library path");
    }

    char prefix[4096];
    size_t prefix_len = (size_t)(parent_base - out);
    size_t need = prefix_len + strlen("backends") + 1 + strlen(arch) + 1;
    if (prefix_len >= sizeof(prefix) || need > out_size) {
        Rf_error("Rsassy backend path is too long");
    }
    memcpy(prefix, out, prefix_len);
    prefix[prefix_len] = '\0';
    snprintf(out, out_size, "%sbackends%c%s", prefix, sep, arch);
}

static void Rsassy_backend_dir(char *out, size_t out_size) {
#ifdef _WIN32
    HMODULE module = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&Rsassy_backend_dir,
                            &module)) {
        Rf_error("failed to locate Rsassy shared library handle");
    }
    DWORD len = GetModuleFileNameA(module, out, (DWORD)out_size);
    if (len == 0 || len >= out_size) {
        Rf_error("failed to locate Rsassy shared library path");
    }
#else
    Dl_info info;
    if (dladdr((void *)&Rsassy_backend_dir, &info) == 0 || info.dli_fname == NULL) {
        Rf_error("failed to locate Rsassy shared library path");
    }
    snprintf(out, out_size, "%s", info.dli_fname);
#endif
    char *slash = Rsassy_last_path_separator(out);
    if (slash == NULL) {
        Rf_error("failed to locate Rsassy library directory");
    }
    *slash = '\0';
    Rsassy_use_backend_dir(out, out_size);
}

static const char *Rsassy_backend_extension(void) {
#if defined(__APPLE__)
    return ".dylib";
#elif defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

static void Rsassy_backend_path(const char *dir, const char *backend, char *out, size_t out_size) {
    if (!Rsassy_known_backend(backend)) {
        Rf_error("unknown Rsassy backend '%s'", backend);
    }

    const char *prefix = "/rsassy_backend_";
    const char *ext = Rsassy_backend_extension();
    size_t need = strlen(dir) + strlen(prefix) + strlen(backend) + strlen(ext) + 1;
    if (need > out_size) {
        Rf_error("Rsassy backend path is too long");
    }
    strcpy(out, dir);
    strcat(out, prefix);
    strcat(out, backend);
    strcat(out, ext);
}

static void Rsassy_available_backends(const char *dir, char *out, size_t out_size) {
    out[0] = '\0';
    for (size_t i = 0; i < RSASSY_ARRAY_LEN(RSASSY_BACKENDS); i++) {
        char path[4096];
        Rsassy_backend_path(dir, RSASSY_BACKENDS[i], path, sizeof(path));
        if (!Rsassy_file_exists(path)) {
            continue;
        }
        if (out[0] != '\0') {
            strncat(out, ",", out_size - strlen(out) - 1);
        }
        strncat(out, RSASSY_BACKENDS[i], out_size - strlen(out) - 1);
    }
}

static void *Rsassy_load_symbol(RsassyBackendHandle handle, const char *name) {
#ifdef _WIN32
    FARPROC sym = GetProcAddress(handle, name);
    if (sym == NULL) {
        Rf_error("failed to load Rsassy backend symbol '%s'", name);
    }
    return (void *)sym;
#else
    dlerror();
    void *sym = dlsym(handle, name);
    const char *err = dlerror();
    if (err != NULL || sym == NULL) {
        Rf_error("failed to load Rsassy backend symbol '%s': %s", name, err == NULL ? "symbol not found" : err);
    }
    return sym;
#endif
}

static int Rsassy_try_load_backend(const char *dir, const char *backend, char *err, size_t err_size) {
    char path[4096];
    Rsassy_backend_path(dir, backend, path, sizeof(path));
    if (!Rsassy_file_exists(path)) {
        snprintf(err, err_size, "backend '%s' is not installed", backend);
        return 0;
    }

#ifdef _WIN32
    RsassyBackendHandle handle = LoadLibraryA(path);
    if (handle == NULL) {
        snprintf(err, err_size, "LoadLibrary failed for %s", path);
        return 0;
    }
#else
    RsassyBackendHandle handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        snprintf(err, err_size, "%s", dlerror());
        return 0;
    }
#endif

    /* The backend handle intentionally stays loaded for the life of the R
     * process.  Unloading and reloading native code is not portable in R. */
    snprintf(Rsassy_selected_backend, sizeof(Rsassy_selected_backend), "%s", backend);
    Rsassy_rsassy_searcher_new = (Rsassy_searcher_new_fn)Rsassy_load_symbol(handle, "rsassy_searcher_new");
    Rsassy_rsassy_searcher_free = (Rsassy_searcher_free_fn)Rsassy_load_symbol(handle, "rsassy_searcher_free");
    Rsassy_rsassy_searcher_search = (Rsassy_searcher_search_fn)Rsassy_load_symbol(handle, "rsassy_searcher_search");
    Rsassy_rsassy_searcher_search_many = (Rsassy_searcher_search_many_fn)Rsassy_load_symbol(handle, "rsassy_searcher_search_many");
    Rsassy_rsassy_crispr_search_many = (Rsassy_crispr_search_many_fn)Rsassy_load_symbol(handle, "rsassy_crispr_search_many");
    Rsassy_rsassy_matches_free = (Rsassy_matches_free_fn)Rsassy_load_symbol(handle, "rsassy_matches_free");
    Rsassy_rsassy_fastx_iter_new = (Rsassy_fastx_iter_new_fn)Rsassy_load_symbol(handle, "rsassy_fastx_iter_new");
    Rsassy_rsassy_fastx_iter_free = (Rsassy_fastx_iter_free_fn)Rsassy_load_symbol(handle, "rsassy_fastx_iter_free");
    Rsassy_rsassy_fastx_iter_next = (Rsassy_fastx_iter_next_fn)Rsassy_load_symbol(handle, "rsassy_fastx_iter_next");
    Rsassy_rsassy_fastx_batch_free = (Rsassy_fastx_batch_free_fn)Rsassy_load_symbol(handle, "rsassy_fastx_batch_free");
    Rsassy_rsassy_fastx_batch_n = (Rsassy_fastx_batch_n_fn)Rsassy_load_symbol(handle, "rsassy_fastx_batch_n");
    Rsassy_rsassy_fastx_batch_has_qual = (Rsassy_fastx_batch_has_qual_fn)Rsassy_load_symbol(handle, "rsassy_fastx_batch_has_qual");
    Rsassy_rsassy_fastx_batch_id_ptr = (Rsassy_fastx_batch_slice_ptr_fn)Rsassy_load_symbol(handle, "rsassy_fastx_batch_id_ptr");
    Rsassy_rsassy_fastx_batch_id_len = (Rsassy_fastx_batch_slice_len_fn)Rsassy_load_symbol(handle, "rsassy_fastx_batch_id_len");
    Rsassy_rsassy_fastx_batch_id_utf8 = (Rsassy_fastx_batch_index_bool_fn)Rsassy_load_symbol(handle, "rsassy_fastx_batch_id_utf8");
    Rsassy_rsassy_fastx_batch_seq_ptr = (Rsassy_fastx_batch_slice_ptr_fn)Rsassy_load_symbol(handle, "rsassy_fastx_batch_seq_ptr");
    Rsassy_rsassy_fastx_batch_seq_len = (Rsassy_fastx_batch_slice_len_fn)Rsassy_load_symbol(handle, "rsassy_fastx_batch_seq_len");
    Rsassy_rsassy_fastx_batch_qual_ptr = (Rsassy_fastx_batch_slice_ptr_fn)Rsassy_load_symbol(handle, "rsassy_fastx_batch_qual_ptr");
    Rsassy_rsassy_fastx_batch_qual_len = (Rsassy_fastx_batch_slice_len_fn)Rsassy_load_symbol(handle, "rsassy_fastx_batch_qual_len");
    Rsassy_rsassy_last_error_message = (Rsassy_last_error_message_fn)Rsassy_load_symbol(handle, "rsassy_last_error_message");
    Rsassy_rsassy_features_string = (Rsassy_features_string_fn)Rsassy_load_symbol(handle, "rsassy_features_string");
    Rsassy_backend_loaded = 1;
    return 1;
}

static void Rsassy_init_dynamic_backend(void) {
    char dir[4096];
    char err[4096] = "";

    Rsassy_backend_dir(dir, sizeof(dir));
    Rsassy_available_backends(dir, Rsassy_available_backends_buf, sizeof(Rsassy_available_backends_buf));

    const char *selected = Rsassy_requested_backend[0] == '\0'
                               ? rsassy_select_backend(Rsassy_available_backends_buf)
                               : Rsassy_requested_backend;
    if (selected == NULL || selected[0] == '\0') {
        Rf_error("failed to load any Rsassy backend from %s; installed_backends='%s'", dir, Rsassy_available_backends_buf);
    }
    if (!Rsassy_try_load_backend(dir, selected, err, sizeof(err))) {
        Rf_error("failed to load selected Rsassy backend '%s': %s", selected, err);
    }
}
#endif

#ifdef __EMSCRIPTEN__
static void Rsassy_init_static_backend(void) {
    const char *installed = "wasm_simd128";
    const char *selected = Rsassy_requested_backend[0] == '\0'
                               ? rsassy_select_backend(installed)
                               : Rsassy_requested_backend;

    if (selected == NULL || selected[0] == '\0') {
        Rf_error("failed to select Rsassy WebAssembly backend");
    }
    if (strcmp(selected, "wasm_simd128") != 0) {
        Rf_error("requested Rsassy backend '%s' is not installed in this webR/WebAssembly build", selected);
    }
    if (!rsassy_backend_supported(selected)) {
        Rf_error("requested Rsassy backend '%s' is not supported on this CPU/runtime", selected);
    }

    snprintf(Rsassy_available_backends_buf, sizeof(Rsassy_available_backends_buf), "%s", installed);
    snprintf(Rsassy_selected_backend, sizeof(Rsassy_selected_backend), "%s", selected);
    Rsassy_backend_loaded = 1;
}
#endif

void Rsassy_init_backend(void) {
    if (Rsassy_backend_loaded) {
        return;
    }

#ifdef __EMSCRIPTEN__
    Rsassy_init_static_backend();
#else
    Rsassy_init_dynamic_backend();
#endif
}
