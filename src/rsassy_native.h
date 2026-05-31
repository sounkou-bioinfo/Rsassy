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
typedef struct RsassyFastxIter RsassyFastxIter;
typedef struct RsassyFastxBatch RsassyFastxBatch;

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
typedef int (*Rsassy_crispr_search_many_fn)(const uint8_t **guides,
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
typedef void (*Rsassy_matches_free_fn)(RsassyMatch *matches, uintptr_t len);
typedef int (*Rsassy_fastx_iter_new_fn)(const char *path,
                                        uintptr_t batch_records,
                                        bool include_qual,
                                        RsassyFastxIter **out);
typedef void (*Rsassy_fastx_iter_free_fn)(RsassyFastxIter *iter);
typedef int (*Rsassy_fastx_iter_next_fn)(RsassyFastxIter *iter, RsassyFastxBatch **out);
typedef void (*Rsassy_fastx_batch_free_fn)(RsassyFastxBatch *batch);
typedef uintptr_t (*Rsassy_fastx_batch_n_fn)(const RsassyFastxBatch *batch);
typedef bool (*Rsassy_fastx_batch_has_qual_fn)(const RsassyFastxBatch *batch);
typedef bool (*Rsassy_fastx_batch_index_bool_fn)(const RsassyFastxBatch *batch, uintptr_t index);
typedef const uint8_t *(*Rsassy_fastx_batch_slice_ptr_fn)(const RsassyFastxBatch *batch, uintptr_t index);
typedef uintptr_t (*Rsassy_fastx_batch_slice_len_fn)(const RsassyFastxBatch *batch, uintptr_t index);
typedef const char *(*Rsassy_last_error_message_fn)(void);
typedef const char *(*Rsassy_features_string_fn)(void);

typedef struct RsassyNativeApi {
    Rsassy_searcher_new_fn searcher_new;
    Rsassy_searcher_free_fn searcher_free;
    Rsassy_searcher_search_fn searcher_search;
    Rsassy_searcher_search_many_fn searcher_search_many;
    Rsassy_crispr_search_many_fn crispr_search_many;
    Rsassy_matches_free_fn matches_free;
    Rsassy_fastx_iter_new_fn fastx_iter_new;
    Rsassy_fastx_iter_free_fn fastx_iter_free;
    Rsassy_fastx_iter_next_fn fastx_iter_next;
    Rsassy_fastx_batch_free_fn fastx_batch_free;
    Rsassy_fastx_batch_n_fn fastx_batch_n;
    Rsassy_fastx_batch_has_qual_fn fastx_batch_has_qual;
    Rsassy_fastx_batch_slice_ptr_fn fastx_batch_id_ptr;
    Rsassy_fastx_batch_slice_len_fn fastx_batch_id_len;
    Rsassy_fastx_batch_index_bool_fn fastx_batch_id_utf8;
    Rsassy_fastx_batch_slice_ptr_fn fastx_batch_seq_ptr;
    Rsassy_fastx_batch_slice_len_fn fastx_batch_seq_len;
    Rsassy_fastx_batch_slice_ptr_fn fastx_batch_qual_ptr;
    Rsassy_fastx_batch_slice_len_fn fastx_batch_qual_len;
    Rsassy_last_error_message_fn last_error_message;
    Rsassy_features_string_fn features_string;
} RsassyNativeApi;

extern RsassyNativeApi Rsassy_native;

extern bool rsassy_backend_supported(const char *name);
extern const char *rsassy_select_backend(const char *available);
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
