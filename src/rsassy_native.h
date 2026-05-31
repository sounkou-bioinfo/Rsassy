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
#else
extern Rsassy_searcher_new_fn Rsassy_rsassy_searcher_new;
extern Rsassy_searcher_free_fn Rsassy_rsassy_searcher_free;
extern Rsassy_searcher_search_fn Rsassy_rsassy_searcher_search;
extern Rsassy_searcher_search_many_fn Rsassy_rsassy_searcher_search_many;
extern Rsassy_crispr_search_many_fn Rsassy_rsassy_crispr_search_many;
extern Rsassy_matches_free_fn Rsassy_rsassy_matches_free;
extern Rsassy_fastx_iter_new_fn Rsassy_rsassy_fastx_iter_new;
extern Rsassy_fastx_iter_free_fn Rsassy_rsassy_fastx_iter_free;
extern Rsassy_fastx_iter_next_fn Rsassy_rsassy_fastx_iter_next;
extern Rsassy_fastx_batch_free_fn Rsassy_rsassy_fastx_batch_free;
extern Rsassy_fastx_batch_n_fn Rsassy_rsassy_fastx_batch_n;
extern Rsassy_fastx_batch_has_qual_fn Rsassy_rsassy_fastx_batch_has_qual;
extern Rsassy_fastx_batch_slice_ptr_fn Rsassy_rsassy_fastx_batch_id_ptr;
extern Rsassy_fastx_batch_slice_len_fn Rsassy_rsassy_fastx_batch_id_len;
extern Rsassy_fastx_batch_index_bool_fn Rsassy_rsassy_fastx_batch_id_utf8;
extern Rsassy_fastx_batch_slice_ptr_fn Rsassy_rsassy_fastx_batch_seq_ptr;
extern Rsassy_fastx_batch_slice_len_fn Rsassy_rsassy_fastx_batch_seq_len;
extern Rsassy_fastx_batch_slice_ptr_fn Rsassy_rsassy_fastx_batch_qual_ptr;
extern Rsassy_fastx_batch_slice_len_fn Rsassy_rsassy_fastx_batch_qual_len;
extern Rsassy_last_error_message_fn Rsassy_rsassy_last_error_message;
extern Rsassy_features_string_fn Rsassy_rsassy_features_string;

#define rsassy_searcher_new Rsassy_rsassy_searcher_new
#define rsassy_searcher_free Rsassy_rsassy_searcher_free
#define rsassy_searcher_search Rsassy_rsassy_searcher_search
#define rsassy_searcher_search_many Rsassy_rsassy_searcher_search_many
#define rsassy_crispr_search_many Rsassy_rsassy_crispr_search_many
#define rsassy_matches_free Rsassy_rsassy_matches_free
#define rsassy_fastx_iter_new Rsassy_rsassy_fastx_iter_new
#define rsassy_fastx_iter_free Rsassy_rsassy_fastx_iter_free
#define rsassy_fastx_iter_next Rsassy_rsassy_fastx_iter_next
#define rsassy_fastx_batch_free Rsassy_rsassy_fastx_batch_free
#define rsassy_fastx_batch_n Rsassy_rsassy_fastx_batch_n
#define rsassy_fastx_batch_has_qual Rsassy_rsassy_fastx_batch_has_qual
#define rsassy_fastx_batch_id_ptr Rsassy_rsassy_fastx_batch_id_ptr
#define rsassy_fastx_batch_id_len Rsassy_rsassy_fastx_batch_id_len
#define rsassy_fastx_batch_id_utf8 Rsassy_rsassy_fastx_batch_id_utf8
#define rsassy_fastx_batch_seq_ptr Rsassy_rsassy_fastx_batch_seq_ptr
#define rsassy_fastx_batch_seq_len Rsassy_rsassy_fastx_batch_seq_len
#define rsassy_fastx_batch_qual_ptr Rsassy_rsassy_fastx_batch_qual_ptr
#define rsassy_fastx_batch_qual_len Rsassy_rsassy_fastx_batch_qual_len
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
