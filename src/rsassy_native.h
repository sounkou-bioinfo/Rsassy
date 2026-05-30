#ifndef RSASSY_NATIVE_H
#define RSASSY_NATIVE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <Rinternals.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RsassySearcher RsassySearcher;

typedef struct RsassyMatch {
    uintptr_t pattern_idx;
    uintptr_t text_idx;
    uintptr_t text_start;
    uintptr_t text_end;
    uintptr_t pattern_start;
    uintptr_t pattern_end;
    int32_t cost;
    uint8_t strand;
    char *cigar;
    char *match_region;
    uintptr_t match_region_len;
} RsassyMatch;

typedef int (*Rsassy_searcher_new_fn)(const char *alphabet, bool rc, float alpha, RsassySearcher **out);
typedef void (*Rsassy_searcher_free_fn)(RsassySearcher *searcher);
typedef int (*Rsassy_searcher_search_fn)(RsassySearcher *searcher,
                                         const uint8_t *pattern,
                                         uintptr_t pattern_len,
                                         const uint8_t *text,
                                         uintptr_t text_len,
                                         uintptr_t k,
                                         bool all_matches,
                                         bool include_match_region,
                                         RsassyMatch **out_matches,
                                         uintptr_t *out_len);
typedef int (*Rsassy_searcher_search_many_fn)(RsassySearcher *searcher,
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
typedef void (*Rsassy_matches_free_fn)(RsassyMatch *matches, uintptr_t len);
typedef const char *(*Rsassy_last_error_message_fn)(void);
typedef const char *(*Rsassy_features_string_fn)(void);

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
extern void rsassy_matches_free(RsassyMatch *matches, uintptr_t len);
extern const char *rsassy_last_error_message(void);
extern const char *rsassy_features_string(void);
#else
extern Rsassy_searcher_new_fn Rsassy_rsassy_searcher_new;
extern Rsassy_searcher_free_fn Rsassy_rsassy_searcher_free;
extern Rsassy_searcher_search_fn Rsassy_rsassy_searcher_search;
extern Rsassy_searcher_search_many_fn Rsassy_rsassy_searcher_search_many;
extern Rsassy_matches_free_fn Rsassy_rsassy_matches_free;
extern Rsassy_last_error_message_fn Rsassy_rsassy_last_error_message;
extern Rsassy_features_string_fn Rsassy_rsassy_features_string;

#define rsassy_searcher_new Rsassy_rsassy_searcher_new
#define rsassy_searcher_free Rsassy_rsassy_searcher_free
#define rsassy_searcher_search Rsassy_rsassy_searcher_search
#define rsassy_searcher_search_many Rsassy_rsassy_searcher_search_many
#define rsassy_matches_free Rsassy_rsassy_matches_free
#define rsassy_last_error_message Rsassy_rsassy_last_error_message
#define rsassy_features_string Rsassy_rsassy_features_string
#endif

extern bool rsassy_backend_supported(const char *name);
extern const char *rsassy_select_backend(const char *available);
int Rsassy_cpu_avx2(void);
int Rsassy_cpu_avx512f(void);
int Rsassy_cpu_neon(void);

void Rsassy_init_backend(void);
int Rsassy_backend_is_loaded(void);
void Rsassy_request_backend(const char *backend);
const char *Rsassy_requested_backend_name(void);
const char *Rsassy_selected_backend_name(void);
const char *Rsassy_available_backend_names(void);
const char *Rsassy_dispatch_mode(void);

#ifdef __cplusplus
}
#endif

#endif
