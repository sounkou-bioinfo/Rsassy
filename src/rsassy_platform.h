#ifndef RSASSY_PLATFORM_H
#define RSASSY_PLATFORM_H

#include <stddef.h>

#include "rsassy_native.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Platform adapters used by the backend loader. Keep OS-specific includes,
 * handles, path rules, dynamic-library calls, and CPU feature probing behind
 * this interface so rsassy_backend.c can stay platform-neutral. */
const char *Rsassy_platform_dispatch_mode(void);
int Rsassy_platform_backend_in_this_build(const char *backend);
void Rsassy_platform_available_backends(const char *const *known_backends,
                                        size_t n_known_backends,
                                        char *out,
                                        size_t out_size);
int Rsassy_platform_load_backend(const char *backend,
                                 RsassyNativeApi *api,
                                 char *err,
                                 size_t err_size);

int Rsassy_cpu_avx2(void);
int Rsassy_cpu_avx512f(void);
int Rsassy_cpu_neon(void);
int Rsassy_cpu_wasm_simd128(void);
int Rsassy_cpu_supports_x86_64_v3(void);
int Rsassy_cpu_supports_x86_64_v4(void);

#ifdef __cplusplus
}
#endif

#endif
