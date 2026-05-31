#include <R.h>

#include "rsassy_native.h"
#include "rsassy_platform.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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

RsassyNativeApi Rsassy_native = {0};

static int Rsassy_backend_loaded = 0;
static char Rsassy_requested_backend[32] = "";
static char Rsassy_selected_backend[32] = "";
static char Rsassy_available_backends_buf[128] = "";

/* Backend names are the public C/R dispatch names. Keep this list small and
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
        return Rsassy_cpu_wasm_simd128() != 0;
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
    return Rsassy_platform_dispatch_mode();
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
    if (!Rsassy_platform_backend_in_this_build(backend)) {
        Rf_error("requested Rsassy backend '%s' is not installed in this %s build", backend, Rsassy_dispatch_mode());
    }
    if (!rsassy_backend_supported(backend)) {
        Rf_error("requested Rsassy backend '%s' is not supported on this CPU/runtime", backend);
    }
    snprintf(Rsassy_requested_backend, sizeof(Rsassy_requested_backend), "%s", backend);
}

static int Rsassy_try_load_backend(const char *backend, char *err, size_t err_size) {
    if (!Rsassy_platform_load_backend(backend, &Rsassy_native, err, err_size)) {
        return 0;
    }

    snprintf(Rsassy_selected_backend, sizeof(Rsassy_selected_backend), "%s", backend);
    Rsassy_backend_loaded = 1;
    return 1;
}

void Rsassy_init_backend(void) {
    if (Rsassy_backend_loaded) {
        return;
    }

    char err[4096] = "";
    Rsassy_platform_available_backends(RSASSY_BACKENDS,
                                       RSASSY_ARRAY_LEN(RSASSY_BACKENDS),
                                       Rsassy_available_backends_buf,
                                       sizeof(Rsassy_available_backends_buf));

    const char *selected = Rsassy_requested_backend[0] == '\0'
                               ? rsassy_select_backend(Rsassy_available_backends_buf)
                               : Rsassy_requested_backend;
    if (selected == NULL || selected[0] == '\0') {
        Rf_error("failed to select an Rsassy backend for this %s build; installed_backends='%s'",
                 Rsassy_dispatch_mode(),
                 Rsassy_available_backend_names());
    }
    if (!Rsassy_try_load_backend(selected, err, sizeof(err))) {
        Rf_error("failed to load selected Rsassy backend '%s': %s", selected, err);
    }
}
