/* glibc exposes dladdr()/Dl_info from <dlfcn.h> only when GNU extensions
 * are requested before any system header is included. This belongs here because
 * locating the loaded shared library is a platform adapter detail. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <R.h>

#include "rsassy_native.h"
#include "rsassy_platform.h"

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

/* ---- Static wasm symbols ------------------------------------------------ */

#ifdef __EMSCRIPTEN__
extern int rsassy_searcher_new(const char *alphabet, bool rc, float alpha, RsassySearcher **out);
extern void rsassy_searcher_free(RsassySearcher *searcher);
extern int rsassy_searcher_search(RsassySearcher *searcher,
                                  const uint8_t *pattern,
                                  uintptr_t pattern_len,
                                  const uint8_t *text,
                                  uintptr_t text_len,
                                  uintptr_t k,
                                  bool all_matches,
                                  bool include_match_region,
                                  RsassyMatch **out_matches,
                                  uintptr_t *out_len);
extern int rsassy_searcher_search_many(RsassySearcher *searcher,
                                       const uint8_t **patterns,
                                       const uintptr_t *pattern_lens,
                                       uintptr_t n_patterns,
                                       const uint8_t **texts,
                                       const uintptr_t *text_lens,
                                       uintptr_t n_texts,
                                       uintptr_t k,
                                       bool all_matches,
                                       uintptr_t threads,
                                       const char *strategy,
                                       bool include_match_region,
                                       RsassyMatch **out_matches,
                                       uintptr_t *out_len);
extern int rsassy_crispr_search_many(const uint8_t **guides,
                                     const uintptr_t *guide_lens,
                                     uintptr_t n_guides,
                                     const uint8_t **texts,
                                     const uintptr_t *text_lens,
                                     uintptr_t n_texts,
                                     uintptr_t k,
                                     uintptr_t pam_length,
                                     bool allow_pam_edits,
                                     float max_n_frac,
                                     bool rc,
                                     uintptr_t threads,
                                     RsassyMatch **out_matches,
                                     uintptr_t *out_len);
extern void rsassy_matches_free(RsassyMatch *matches, uintptr_t len);
extern int rsassy_fastx_iter_new(const char *path,
                                 uintptr_t batch_records,
                                 bool include_qual,
                                 RsassyFastxIter **out);
extern void rsassy_fastx_iter_free(RsassyFastxIter *iter);
extern int rsassy_fastx_iter_next(RsassyFastxIter *iter, RsassyFastxBatch **out);
extern void rsassy_fastx_batch_free(RsassyFastxBatch *batch);
extern uintptr_t rsassy_fastx_batch_n(const RsassyFastxBatch *batch);
extern bool rsassy_fastx_batch_has_qual(const RsassyFastxBatch *batch);
extern const uint8_t *rsassy_fastx_batch_id_ptr(const RsassyFastxBatch *batch, uintptr_t index);
extern uintptr_t rsassy_fastx_batch_id_len(const RsassyFastxBatch *batch, uintptr_t index);
extern bool rsassy_fastx_batch_id_utf8(const RsassyFastxBatch *batch, uintptr_t index);
extern const uint8_t *rsassy_fastx_batch_seq_ptr(const RsassyFastxBatch *batch, uintptr_t index);
extern uintptr_t rsassy_fastx_batch_seq_len(const RsassyFastxBatch *batch, uintptr_t index);
extern const uint8_t *rsassy_fastx_batch_qual_ptr(const RsassyFastxBatch *batch, uintptr_t index);
extern uintptr_t rsassy_fastx_batch_qual_len(const RsassyFastxBatch *batch, uintptr_t index);
extern const char *rsassy_last_error_message(void);
extern const char *rsassy_features_string(void);
#endif

/* ---- Runtime CPU feature checks -----------------------------------------
 *
 * Rsassy builds each native Rust backend with a target CPU level. The loader
 * checks the matching runtime CPU and OS state before it ever loads a backend.
 * Direct CPUID/XGETBV avoids compiler builtin spelling differences across GCC,
 * LLVM, Apple clang, and Windows toolchains.
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

int Rsassy_cpu_supports_x86_64_v3(void) {
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

int Rsassy_cpu_supports_x86_64_v4(void) {
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

int Rsassy_cpu_supports_x86_64_v3(void) {
    return 0;
}

int Rsassy_cpu_supports_x86_64_v4(void) {
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

int Rsassy_cpu_wasm_simd128(void) {
#ifdef __EMSCRIPTEN__
    return 1;
#else
    return 0;
#endif
}

/* ---- Platform-neutral backend API --------------------------------------- */

const char *Rsassy_platform_dispatch_mode(void) {
#ifdef __EMSCRIPTEN__
    return "static";
#else
    return "dynamic";
#endif
}

int Rsassy_platform_backend_in_this_build(const char *backend) {
    if (backend == NULL) {
        return 0;
    }
#ifdef __EMSCRIPTEN__
    return strcmp(backend, "wasm_simd128") == 0;
#else
    return strcmp(backend, "wasm_simd128") != 0;
#endif
}

static void Rsassy_platform_append_csv(char *out, size_t out_size, const char *value) {
    if (out[0] != '\0') {
        strncat(out, ",", out_size - strlen(out) - 1);
    }
    strncat(out, value, out_size - strlen(out) - 1);
}

/* ---- Dynamic library and path helpers ---------------------------------- */

static int Rsassy_platform_file_exists(const char *path) {
#ifdef _WIN32
    return _access(path, 4) == 0;
#elif defined(__EMSCRIPTEN__)
    (void)path;
    return 0;
#else
    return access(path, R_OK) == 0;
#endif
}

static char *Rsassy_platform_last_path_separator(char *path) {
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
static void Rsassy_platform_use_backend_dir(char *out, size_t out_size) {
    char *slash = Rsassy_platform_last_path_separator(out);
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

    char *parent_slash = Rsassy_platform_last_path_separator(out);
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

static void Rsassy_platform_backend_dir(char *out, size_t out_size) {
#ifdef _WIN32
    HMODULE module = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&Rsassy_platform_backend_dir,
                            &module)) {
        Rf_error("failed to locate Rsassy shared library handle");
    }
    DWORD len = GetModuleFileNameA(module, out, (DWORD)out_size);
    if (len == 0 || len >= out_size) {
        Rf_error("failed to locate Rsassy shared library path");
    }
#elif defined(__EMSCRIPTEN__)
    (void)out;
    (void)out_size;
    Rf_error("dynamic Rsassy backend directories are not used in WebAssembly builds");
#else
    Dl_info info;
    if (dladdr((void *)&Rsassy_platform_backend_dir, &info) == 0 || info.dli_fname == NULL) {
        Rf_error("failed to locate Rsassy shared library path");
    }
    snprintf(out, out_size, "%s", info.dli_fname);
#endif

#ifndef __EMSCRIPTEN__
    char *slash = Rsassy_platform_last_path_separator(out);
    if (slash == NULL) {
        Rf_error("failed to locate Rsassy library directory");
    }
    *slash = '\0';
    Rsassy_platform_use_backend_dir(out, out_size);
#endif
}

static const char *Rsassy_platform_backend_extension(void) {
#if defined(__APPLE__)
    return ".dylib";
#elif defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

static void Rsassy_platform_backend_path(const char *dir, const char *backend, char *out, size_t out_size) {
    const char *prefix = "/rsassy_backend_";
    const char *ext = Rsassy_platform_backend_extension();
    size_t need = strlen(dir) + strlen(prefix) + strlen(backend) + strlen(ext) + 1;
    if (need > out_size) {
        Rf_error("Rsassy backend path is too long");
    }
    strcpy(out, dir);
    strcat(out, prefix);
    strcat(out, backend);
    strcat(out, ext);
}

void Rsassy_platform_available_backends(const char *const *known_backends,
                                        size_t n_known_backends,
                                        char *out,
                                        size_t out_size) {
    out[0] = '\0';
#ifdef __EMSCRIPTEN__
    for (size_t i = 0; i < n_known_backends; i++) {
        if (Rsassy_platform_backend_in_this_build(known_backends[i])) {
            Rsassy_platform_append_csv(out, out_size, known_backends[i]);
        }
    }
#else
    char dir[4096];
    Rsassy_platform_backend_dir(dir, sizeof(dir));
    for (size_t i = 0; i < n_known_backends; i++) {
        if (!Rsassy_platform_backend_in_this_build(known_backends[i])) {
            continue;
        }
        char path[4096];
        Rsassy_platform_backend_path(dir, known_backends[i], path, sizeof(path));
        if (Rsassy_platform_file_exists(path)) {
            Rsassy_platform_append_csv(out, out_size, known_backends[i]);
        }
    }
#endif
}

static void *Rsassy_platform_load_library(const char *path, char *err, size_t err_size) {
#ifdef _WIN32
    HMODULE handle = LoadLibraryA(path);
    if (handle == NULL) {
        snprintf(err, err_size, "LoadLibrary failed for %s", path);
    }
    return (void *)handle;
#elif defined(__EMSCRIPTEN__)
    (void)path;
    snprintf(err, err_size, "dynamic backend loading is not available in WebAssembly builds");
    return NULL;
#else
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        snprintf(err, err_size, "%s", dlerror());
    }
    return handle;
#endif
}

static void *Rsassy_platform_load_symbol(void *handle, const char *name) {
#ifdef _WIN32
    FARPROC sym = GetProcAddress((HMODULE)handle, name);
    if (sym == NULL) {
        Rf_error("failed to load Rsassy backend symbol '%s'", name);
    }
    return (void *)sym;
#elif defined(__EMSCRIPTEN__)
    (void)handle;
    Rf_error("failed to load Rsassy backend symbol '%s': dynamic loading is not available in WebAssembly builds", name);
    return NULL;
#else
    /* Clear any stale loader error before dlsym(); dlsym() failure is reported
     * by the following dlerror() call, and a stale error would be misleading. */
    dlerror();
    void *sym = dlsym(handle, name);
    const char *err = dlerror();
    if (err != NULL || sym == NULL) {
        Rf_error("failed to load Rsassy backend symbol '%s': %s", name, err == NULL ? "symbol not found" : err);
    }
    return sym;
#endif
}

static void Rsassy_platform_load_api_symbols(void *handle, RsassyNativeApi *api) {
    api->searcher_new = (Rsassy_searcher_new_fn)Rsassy_platform_load_symbol(handle, "rsassy_searcher_new");
    api->searcher_free = (Rsassy_searcher_free_fn)Rsassy_platform_load_symbol(handle, "rsassy_searcher_free");
    api->searcher_search = (Rsassy_searcher_search_fn)Rsassy_platform_load_symbol(handle, "rsassy_searcher_search");
    api->searcher_search_many = (Rsassy_searcher_search_many_fn)Rsassy_platform_load_symbol(handle, "rsassy_searcher_search_many");
    api->crispr_search_many = (Rsassy_crispr_search_many_fn)Rsassy_platform_load_symbol(handle, "rsassy_crispr_search_many");
    api->matches_free = (Rsassy_matches_free_fn)Rsassy_platform_load_symbol(handle, "rsassy_matches_free");
    api->fastx_iter_new = (Rsassy_fastx_iter_new_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_iter_new");
    api->fastx_iter_free = (Rsassy_fastx_iter_free_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_iter_free");
    api->fastx_iter_next = (Rsassy_fastx_iter_next_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_iter_next");
    api->fastx_batch_free = (Rsassy_fastx_batch_free_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_batch_free");
    api->fastx_batch_n = (Rsassy_fastx_batch_n_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_batch_n");
    api->fastx_batch_has_qual = (Rsassy_fastx_batch_has_qual_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_batch_has_qual");
    api->fastx_batch_id_ptr = (Rsassy_fastx_batch_slice_ptr_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_batch_id_ptr");
    api->fastx_batch_id_len = (Rsassy_fastx_batch_slice_len_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_batch_id_len");
    api->fastx_batch_id_utf8 = (Rsassy_fastx_batch_index_bool_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_batch_id_utf8");
    api->fastx_batch_seq_ptr = (Rsassy_fastx_batch_slice_ptr_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_batch_seq_ptr");
    api->fastx_batch_seq_len = (Rsassy_fastx_batch_slice_len_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_batch_seq_len");
    api->fastx_batch_qual_ptr = (Rsassy_fastx_batch_slice_ptr_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_batch_qual_ptr");
    api->fastx_batch_qual_len = (Rsassy_fastx_batch_slice_len_fn)Rsassy_platform_load_symbol(handle, "rsassy_fastx_batch_qual_len");
    api->last_error_message = (Rsassy_last_error_message_fn)Rsassy_platform_load_symbol(handle, "rsassy_last_error_message");
    api->features_string = (Rsassy_features_string_fn)Rsassy_platform_load_symbol(handle, "rsassy_features_string");
}

static int Rsassy_platform_load_dynamic_backend(const char *backend, RsassyNativeApi *api, char *err, size_t err_size) {
    char dir[4096];
    char path[4096];
    Rsassy_platform_backend_dir(dir, sizeof(dir));
    Rsassy_platform_backend_path(dir, backend, path, sizeof(path));
    if (!Rsassy_platform_file_exists(path)) {
        snprintf(err, err_size, "backend '%s' is not installed", backend);
        return 0;
    }

    void *handle = Rsassy_platform_load_library(path, err, err_size);
    if (handle == NULL) {
        return 0;
    }

    /* The backend handle intentionally stays loaded for the life of the R
     * process. Unloading and reloading native code is not portable in R. */
    Rsassy_platform_load_api_symbols(handle, api);
    return 1;
}

static int Rsassy_platform_load_static_backend(const char *backend, RsassyNativeApi *api, char *err, size_t err_size) {
#ifdef __EMSCRIPTEN__
    if (strcmp(backend, "wasm_simd128") != 0) {
        snprintf(err, err_size, "backend '%s' is not installed in this WebAssembly build", backend);
        return 0;
    }
    api->searcher_new = rsassy_searcher_new;
    api->searcher_free = rsassy_searcher_free;
    api->searcher_search = rsassy_searcher_search;
    api->searcher_search_many = rsassy_searcher_search_many;
    api->crispr_search_many = rsassy_crispr_search_many;
    api->matches_free = rsassy_matches_free;
    api->fastx_iter_new = rsassy_fastx_iter_new;
    api->fastx_iter_free = rsassy_fastx_iter_free;
    api->fastx_iter_next = rsassy_fastx_iter_next;
    api->fastx_batch_free = rsassy_fastx_batch_free;
    api->fastx_batch_n = rsassy_fastx_batch_n;
    api->fastx_batch_has_qual = rsassy_fastx_batch_has_qual;
    api->fastx_batch_id_ptr = rsassy_fastx_batch_id_ptr;
    api->fastx_batch_id_len = rsassy_fastx_batch_id_len;
    api->fastx_batch_id_utf8 = rsassy_fastx_batch_id_utf8;
    api->fastx_batch_seq_ptr = rsassy_fastx_batch_seq_ptr;
    api->fastx_batch_seq_len = rsassy_fastx_batch_seq_len;
    api->fastx_batch_qual_ptr = rsassy_fastx_batch_qual_ptr;
    api->fastx_batch_qual_len = rsassy_fastx_batch_qual_len;
    api->last_error_message = rsassy_last_error_message;
    api->features_string = rsassy_features_string;
    return 1;
#else
    (void)backend;
    (void)api;
    snprintf(err, err_size, "static backend loading is not available in this native build");
    return 0;
#endif
}

int Rsassy_platform_load_backend(const char *backend, RsassyNativeApi *api, char *err, size_t err_size) {
    err[0] = '\0';
#ifdef __EMSCRIPTEN__
    return Rsassy_platform_load_static_backend(backend, api, err, err_size);
#else
    return Rsassy_platform_load_dynamic_backend(backend, api, err, err_size);
#endif
}
