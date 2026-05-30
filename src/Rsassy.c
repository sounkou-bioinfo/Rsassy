#define _GNU_SOURCE

#include <R.h>
#include <R_ext/Rdynload.h>
#include <Rinternals.h>

#include "rsassy_native.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

/* Lightweight views over R sequence inputs.  They borrow R-owned memory and are
 * only valid while the corresponding SEXP is protected by the caller. */
struct RsassySeqView {
    const uint8_t *data;
    uintptr_t len;
};

struct RsassySeqViews {
    const uint8_t **data;
    uintptr_t *len;
    uintptr_t n;
};

static void Rsassy_stop_last_error(void) {
    const char *message = rsassy_last_error_message();
    if (message == NULL || message[0] == '\0') {
        message = "unknown Rsassy native error";
    }
    Rf_error("%s", message);
}

static void Rsassy_searcher_finalizer(SEXP xp) {
    RsassySearcher *searcher = (RsassySearcher *)R_ExternalPtrAddr(xp);
    if (searcher != NULL) {
        rsassy_searcher_free(searcher);
        R_ClearExternalPtr(xp);
    }
}

static const char *Rsassy_string_scalar(SEXP x, const char *arg) {
    if (TYPEOF(x) != STRSXP || XLENGTH(x) != 1 || STRING_ELT(x, 0) == NA_STRING) {
        Rf_error("%s must be a non-missing character scalar", arg);
    }
    return CHAR(STRING_ELT(x, 0));
}

static int Rsassy_logical_scalar(SEXP x, const char *arg) {
    int out = Rf_asLogical(x);
    if (out == NA_LOGICAL) {
        Rf_error("%s must be TRUE or FALSE", arg);
    }
    return out;
}

SEXP RC_sassy_set_backend(SEXP backend_s) {
    const char *backend = Rsassy_string_scalar(backend_s, "backend");
    Rsassy_request_backend(backend);
    return Rf_ScalarString(Rf_mkChar(Rsassy_requested_backend_name()));
}

static double Rsassy_alpha_scalar(SEXP x) {
    if (x == R_NilValue) {
        return NAN;
    }
    if (TYPEOF(x) != REALSXP && TYPEOF(x) != INTSXP) {
        Rf_error("alpha must be NULL or a numeric scalar");
    }
    if (XLENGTH(x) != 1) {
        Rf_error("alpha must be NULL or a numeric scalar");
    }
    double out = Rf_asReal(x);
    if (!R_FINITE(out) || out < 0 || out > 1) {
        Rf_error("alpha must be NULL or a single number in [0, 1]");
    }
    return out;
}

static uintptr_t Rsassy_uintptr_scalar(SEXP x, const char *arg, uintptr_t min_value) {
    double value = Rf_asReal(x);
    if (!R_FINITE(value) || value < (double)min_value || floor(value) != value || value > (double)SIZE_MAX) {
        Rf_error("%s must be a finite integer-like value >= %llu", arg, (unsigned long long)min_value);
    }
    return (uintptr_t)value;
}

SEXP RC_sassy_searcher_new(SEXP alphabet_s, SEXP rc_s, SEXP alpha_s) {
    Rsassy_init_backend();
    const char *alphabet = Rsassy_string_scalar(alphabet_s, "alphabet");
    int rc = Rsassy_logical_scalar(rc_s, "rc");
    double alpha = Rsassy_alpha_scalar(alpha_s);
    float alpha_f = isnan(alpha) ? NAN : (float)alpha;

    RsassySearcher *searcher = NULL;
    if (rsassy_searcher_new(alphabet, rc == TRUE, alpha_f, &searcher) != 0) {
        Rsassy_stop_last_error();
    }

    SEXP xp = PROTECT(R_MakeExternalPtr(searcher, R_NilValue, R_NilValue));
    R_RegisterCFinalizerEx(xp, Rsassy_searcher_finalizer, TRUE);

    SEXP klass = PROTECT(Rf_mkString("sassy_searcher"));
    SEXP rc_attr = PROTECT(Rf_ScalarLogical(rc == TRUE));
    SEXP alpha_attr = PROTECT(Rf_ScalarReal(alpha));
    Rf_setAttrib(xp, R_ClassSymbol, klass);
    Rf_setAttrib(xp, Rf_install("alphabet"), alphabet_s);
    Rf_setAttrib(xp, Rf_install("rc"), rc_attr);
    Rf_setAttrib(xp, Rf_install("alpha"), alpha_attr);

    UNPROTECT(4);
    return xp;
}

static struct RsassySeqView Rsassy_sequence_view(SEXP x, const char *arg) {
    struct RsassySeqView view;
    view.data = NULL;
    view.len = 0;

    if (TYPEOF(x) == RAWSXP) {
        R_xlen_t len = XLENGTH(x);
        if ((uint64_t)len > (uint64_t)SIZE_MAX) {
            Rf_error("%s is too large for this platform", arg);
        }
        view.len = (uintptr_t)len;
        if (len > 0) {
            /* DATAPTR_OR_NULL lets ALTREP raw-vector implementations expose an
             * existing contiguous buffer without forcing materialization. If no
             * contiguous pointer is available, DATAPTR_RO may materialize. */
            const void *ptr = DATAPTR_OR_NULL(x);
            if (ptr == NULL) {
                ptr = DATAPTR_RO(x);
            }
            view.data = (const uint8_t *)ptr;
        }
        return view;
    }

    if (TYPEOF(x) == STRSXP && XLENGTH(x) == 1 && STRING_ELT(x, 0) != NA_STRING) {
        SEXP ch = STRING_ELT(x, 0);
        R_xlen_t len = XLENGTH(ch);
        if ((uint64_t)len > (uint64_t)SIZE_MAX) {
            Rf_error("%s is too large for this platform", arg);
        }
        view.len = (uintptr_t)len;
        view.data = len > 0 ? (const uint8_t *)CHAR(ch) : NULL;
        return view;
    }

    Rf_error("%s must be a raw vector or a non-missing character scalar", arg);
    return view;
}

static void Rsassy_fill_sequence_view(SEXP x, const char *arg, struct RsassySeqView *view) {
    *view = Rsassy_sequence_view(x, arg);
}

static struct RsassySeqViews Rsassy_sequences_view(SEXP x, const char *arg) {
    struct RsassySeqViews views;
    views.data = NULL;
    views.len = NULL;
    views.n = 0;

    if (TYPEOF(x) == RAWSXP) {
        views.n = 1;
        views.data = (const uint8_t **)R_alloc(1, sizeof(const uint8_t *));
        views.len = (uintptr_t *)R_alloc(1, sizeof(uintptr_t));
        struct RsassySeqView view = Rsassy_sequence_view(x, arg);
        views.data[0] = view.data;
        views.len[0] = view.len;
        return views;
    }

    if (TYPEOF(x) == STRSXP) {
        R_xlen_t n = XLENGTH(x);
        if ((uint64_t)n > (uint64_t)SIZE_MAX) {
            Rf_error("%s has too many elements for this platform", arg);
        }
        views.n = (uintptr_t)n;
        views.data = (const uint8_t **)R_alloc((size_t)n, sizeof(const uint8_t *));
        views.len = (uintptr_t *)R_alloc((size_t)n, sizeof(uintptr_t));
        for (R_xlen_t i = 0; i < n; i++) {
            if (STRING_ELT(x, i) == NA_STRING) {
                Rf_error("%s must not contain NA strings", arg);
            }
            SEXP ch = STRING_ELT(x, i);
            R_xlen_t len = XLENGTH(ch);
            if ((uint64_t)len > (uint64_t)SIZE_MAX) {
                Rf_error("%s[[%lld]] is too large for this platform", arg, (long long)i + 1);
            }
            views.data[i] = len > 0 ? (const uint8_t *)CHAR(ch) : NULL;
            views.len[i] = (uintptr_t)len;
        }
        return views;
    }

    if (TYPEOF(x) == VECSXP) {
        R_xlen_t n = XLENGTH(x);
        if ((uint64_t)n > (uint64_t)SIZE_MAX) {
            Rf_error("%s has too many elements for this platform", arg);
        }
        views.n = (uintptr_t)n;
        views.data = (const uint8_t **)R_alloc((size_t)n, sizeof(const uint8_t *));
        views.len = (uintptr_t *)R_alloc((size_t)n, sizeof(uintptr_t));
        for (R_xlen_t i = 0; i < n; i++) {
            char item_arg[128];
            snprintf(item_arg, sizeof(item_arg), "%s[[%lld]]", arg, (long long)i + 1);
            struct RsassySeqView view;
            Rsassy_fill_sequence_view(VECTOR_ELT(x, i), item_arg, &view);
            views.data[i] = view.data;
            views.len[i] = view.len;
        }
        return views;
    }

    Rf_error("%s must be a raw vector, character vector, or list of raw/character scalars", arg);
    return views;
}

static RsassySearcher *Rsassy_searcher_from_xptr(SEXP xp) {
    if (TYPEOF(xp) != EXTPTRSXP) {
        Rf_error("searcher must be an external pointer created by sassy_searcher()");
    }
    RsassySearcher *searcher = (RsassySearcher *)R_ExternalPtrAddr(xp);
    if (searcher == NULL) {
        Rf_error("sassy searcher pointer is no longer valid");
    }
    return searcher;
}

static bool Rsassy_strategy_is_pairwise(const char *strategy) {
    return strcmp(strategy, "pairwise") == 0;
}

static bool Rsassy_strategy_is_known(const char *strategy) {
    return Rsassy_strategy_is_pairwise(strategy) ||
           strcmp(strategy, "batch_texts") == 0 ||
           strcmp(strategy, "batch_patterns") == 0 ||
           strcmp(strategy, "encoded_patterns") == 0 ||
           strcmp(strategy, "v2") == 0;
}

static void Rsassy_check_strategy_request(const char *strategy, int all) {
    if (!Rsassy_strategy_is_known(strategy)) {
        Rf_error("unsupported search strategy: %s; expected 'pairwise', 'batch_texts', 'batch_patterns', 'encoded_patterns', or 'v2'", strategy);
    }
    if (all == TRUE && !Rsassy_strategy_is_pairwise(strategy)) {
        Rf_error("all = TRUE maps to sassy::Searcher::search_all() and requires strategy = 'pairwise', not '%s'", strategy);
    }
}

static int Rsassy_cmp_uintptr(uintptr_t a, uintptr_t b) {
    return (a > b) - (a < b);
}

static int Rsassy_cmp_int32(int32_t a, int32_t b) {
    return (a > b) - (a < b);
}

static int Rsassy_cmp_cstr(const char *a, const char *b) {
    if (a == NULL) {
        a = "";
    }
    if (b == NULL) {
        b = "";
    }
    return strcmp(a, b);
}

static int Rsassy_match_compare_input_order(const void *lhs, const void *rhs) {
    const RsassyMatch *a = (const RsassyMatch *)lhs;
    const RsassyMatch *b = (const RsassyMatch *)rhs;
    int cmp;

    cmp = Rsassy_cmp_uintptr(a->text_idx, b->text_idx);
    if (cmp != 0) return cmp;
    cmp = Rsassy_cmp_uintptr(a->text_start, b->text_start);
    if (cmp != 0) return cmp;
    cmp = Rsassy_cmp_uintptr(a->text_end, b->text_end);
    if (cmp != 0) return cmp;
    cmp = Rsassy_cmp_uintptr(a->pattern_idx, b->pattern_idx);
    if (cmp != 0) return cmp;
    cmp = Rsassy_cmp_uintptr(a->pattern_start, b->pattern_start);
    if (cmp != 0) return cmp;
    cmp = Rsassy_cmp_uintptr(a->pattern_end, b->pattern_end);
    if (cmp != 0) return cmp;
    cmp = Rsassy_cmp_int32(a->cost, b->cost);
    if (cmp != 0) return cmp;
    cmp = ((int)a->strand > (int)b->strand) - ((int)a->strand < (int)b->strand);
    if (cmp != 0) return cmp;
    return Rsassy_cmp_cstr(a->cigar, b->cigar);
}

static SEXP Rsassy_matches_data_frame(RsassyMatch *matches, uintptr_t n, bool include_match_region) {
    if ((uint64_t)n > (uint64_t)INT_MAX) {
        Rf_error("too many matches to return as an R data frame");
    }
    if (n > 1) {
        qsort(matches, (size_t)n, sizeof(RsassyMatch), Rsassy_match_compare_input_order);
    }

    int ncol = include_match_region ? 10 : 9;
    int nprotect = 0;
    R_xlen_t rn = (R_xlen_t)n;
    SEXP pattern_idx = PROTECT(Rf_allocVector(INTSXP, rn)); nprotect++;
    SEXP text_idx = PROTECT(Rf_allocVector(INTSXP, rn)); nprotect++;
    SEXP text_start = PROTECT(Rf_allocVector(REALSXP, rn)); nprotect++;
    SEXP text_end = PROTECT(Rf_allocVector(REALSXP, rn)); nprotect++;
    SEXP pattern_start = PROTECT(Rf_allocVector(REALSXP, rn)); nprotect++;
    SEXP pattern_end = PROTECT(Rf_allocVector(REALSXP, rn)); nprotect++;
    SEXP cost = PROTECT(Rf_allocVector(INTSXP, rn)); nprotect++;
    SEXP strand = PROTECT(Rf_allocVector(STRSXP, rn)); nprotect++;
    SEXP cigar = PROTECT(Rf_allocVector(STRSXP, rn)); nprotect++;
    SEXP match_region = R_NilValue;
    if (include_match_region) {
        match_region = PROTECT(Rf_allocVector(STRSXP, rn)); nprotect++;
    }

    for (R_xlen_t i = 0; i < rn; i++) {
        if ((uint64_t)matches[i].pattern_idx > (uint64_t)INT_MAX ||
            (uint64_t)matches[i].text_idx > (uint64_t)INT_MAX) {
            Rf_error("pattern_idx/text_idx are too large to return as R integers");
        }
        INTEGER(pattern_idx)[i] = (int)matches[i].pattern_idx;
        INTEGER(text_idx)[i] = (int)matches[i].text_idx;
        REAL(text_start)[i] = (double)matches[i].text_start;
        REAL(text_end)[i] = (double)matches[i].text_end;
        REAL(pattern_start)[i] = (double)matches[i].pattern_start;
        REAL(pattern_end)[i] = (double)matches[i].pattern_end;
        INTEGER(cost)[i] = (int)matches[i].cost;
        SET_STRING_ELT(strand, i, Rf_mkChar(matches[i].strand == 0 ? "+" : "-"));
        SET_STRING_ELT(cigar, i, Rf_mkChar(matches[i].cigar == NULL ? "" : matches[i].cigar));
        if (include_match_region) {
            if ((uint64_t)matches[i].match_region_len > (uint64_t)INT_MAX) {
                Rf_error("match_region is too large to return as an R string");
            }
            const char *region = matches[i].match_region == NULL ? "" : matches[i].match_region;
            SET_STRING_ELT(match_region, i, Rf_mkCharLenCE(region, (int)matches[i].match_region_len, CE_BYTES));
        }
    }

    SEXP out = PROTECT(Rf_allocVector(VECSXP, ncol)); nprotect++;
    SET_VECTOR_ELT(out, 0, pattern_idx);
    SET_VECTOR_ELT(out, 1, text_idx);
    SET_VECTOR_ELT(out, 2, text_start);
    SET_VECTOR_ELT(out, 3, text_end);
    SET_VECTOR_ELT(out, 4, pattern_start);
    SET_VECTOR_ELT(out, 5, pattern_end);
    SET_VECTOR_ELT(out, 6, cost);
    SET_VECTOR_ELT(out, 7, strand);
    SET_VECTOR_ELT(out, 8, cigar);
    if (include_match_region) {
        SET_VECTOR_ELT(out, 9, match_region);
    }

    SEXP names = PROTECT(Rf_allocVector(STRSXP, ncol)); nprotect++;
    SET_STRING_ELT(names, 0, Rf_mkChar("pattern_idx"));
    SET_STRING_ELT(names, 1, Rf_mkChar("text_idx"));
    SET_STRING_ELT(names, 2, Rf_mkChar("text_start"));
    SET_STRING_ELT(names, 3, Rf_mkChar("text_end"));
    SET_STRING_ELT(names, 4, Rf_mkChar("pattern_start"));
    SET_STRING_ELT(names, 5, Rf_mkChar("pattern_end"));
    SET_STRING_ELT(names, 6, Rf_mkChar("cost"));
    SET_STRING_ELT(names, 7, Rf_mkChar("strand"));
    SET_STRING_ELT(names, 8, Rf_mkChar("cigar"));
    if (include_match_region) {
        SET_STRING_ELT(names, 9, Rf_mkChar("match_region"));
    }
    Rf_setAttrib(out, R_NamesSymbol, names);

    SEXP row_names = PROTECT(Rf_allocVector(INTSXP, 2)); nprotect++;
    INTEGER(row_names)[0] = NA_INTEGER;
    INTEGER(row_names)[1] = -(int)n;
    Rf_setAttrib(out, R_RowNamesSymbol, row_names);

    SEXP klass = PROTECT(Rf_allocVector(STRSXP, 2)); nprotect++;
    SET_STRING_ELT(klass, 0, Rf_mkChar("sassy_matches"));
    SET_STRING_ELT(klass, 1, Rf_mkChar("data.frame"));
    Rf_setAttrib(out, R_ClassSymbol, klass);

    UNPROTECT(nprotect);
    return out;
}

SEXP RC_sassy_searcher_search(SEXP searcher_s,
                              SEXP pattern_s,
                              SEXP text_s,
                              SEXP k_s,
                              SEXP all_s,
                              SEXP threads_s,
                              SEXP strategy_s,
                              SEXP match_region_s) {
    Rsassy_init_backend();
    RsassySearcher *searcher = Rsassy_searcher_from_xptr(searcher_s);

    uintptr_t k = Rsassy_uintptr_scalar(k_s, "k", 0);
    int all = Rsassy_logical_scalar(all_s, "all");
    uintptr_t threads = Rsassy_uintptr_scalar(threads_s, "threads", 1);
    const char *strategy = Rsassy_string_scalar(strategy_s, "strategy");
    Rsassy_check_strategy_request(strategy, all);
    int include_match_region = Rsassy_logical_scalar(match_region_s, "match_region");

    struct RsassySeqViews patterns = Rsassy_sequences_view(pattern_s, "pattern");
    struct RsassySeqViews texts = Rsassy_sequences_view(text_s, "text");

    RsassyMatch *matches = NULL;
    uintptr_t n_matches = 0;
    if (patterns.n == 1 && texts.n == 1 && threads == 1 && Rsassy_strategy_is_pairwise(strategy)) {
        if (rsassy_searcher_search(searcher,
                                   patterns.data[0],
                                   patterns.len[0],
                                   texts.data[0],
                                   texts.len[0],
                                   k,
                                   all == TRUE,
                                   include_match_region == TRUE,
                                   &matches,
                                   &n_matches) != 0) {
            Rsassy_stop_last_error();
        }
    } else {
        if (rsassy_searcher_search_many(searcher,
                                        patterns.data,
                                        patterns.len,
                                        patterns.n,
                                        texts.data,
                                        texts.len,
                                        texts.n,
                                        k,
                                        all == TRUE,
                                        threads,
                                        strategy,
                                        include_match_region == TRUE,
                                        &matches,
                                        &n_matches) != 0) {
            Rsassy_stop_last_error();
        }
    }

    SEXP out = Rsassy_matches_data_frame(matches, n_matches, include_match_region == TRUE);
    rsassy_matches_free(matches, n_matches);
    return out;
}

/* Feature reporting: C owns dispatch/runtime facts, Rust reports facts about
 * the loaded backend. Backend sets are returned to R as character vectors. */
static int Rsassy_next_backend_token(const char **cursor, char *out, size_t out_size) {
    const char *p = *cursor == NULL ? "" : *cursor;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == ',') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char *start = p;
        while (*p != '\0' && *p != ',') {
            p++;
        }
        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        if (end > start) {
            size_t len = (size_t)(end - start);
            if (len >= out_size) {
                Rf_error("Rsassy backend name is too long");
            }
            memcpy(out, start, len);
            out[len] = '\0';
            *cursor = p;
            return 1;
        }
    }
    *cursor = p;
    return 0;
}

static size_t Rsassy_backend_count(const char *csv, int supported_only) {
    if (csv == NULL || csv[0] == '\0' || strcmp(csv, "none") == 0) {
        return 0;
    }

    size_t n = 0;
    char backend[64];
    const char *cursor = csv;
    while (Rsassy_next_backend_token(&cursor, backend, sizeof(backend))) {
        if (!supported_only || rsassy_backend_supported(backend)) {
            n++;
        }
    }
    return n;
}

static SEXP Rsassy_backend_vector(const char *csv, int supported_only) {
    size_t n = Rsassy_backend_count(csv, supported_only);
    SEXP out = PROTECT(Rf_allocVector(STRSXP, (R_xlen_t)n));

    char backend[64];
    const char *cursor = n == 0 ? "" : csv;
    for (size_t i = 0; Rsassy_next_backend_token(&cursor, backend, sizeof(backend));) {
        if (!supported_only || rsassy_backend_supported(backend)) {
            SET_STRING_ELT(out, (R_xlen_t)i, Rf_mkChar(backend));
            i++;
        }
    }

    UNPROTECT(1);
    return out;
}

static SEXP Rsassy_feature_value(const char *value) {
    if (strcmp(value, "true") == 0) {
        return Rf_ScalarLogical(TRUE);
    }
    if (strcmp(value, "false") == 0) {
        return Rf_ScalarLogical(FALSE);
    }
    if (strcmp(value, "NA") == 0) {
        return Rf_ScalarLogical(NA_LOGICAL);
    }
    return Rf_mkString(value);
}

static void Rsassy_set_feature(SEXP out, SEXP names, R_xlen_t *i, const char *name, SEXP value) {
    SET_STRING_ELT(names, *i, Rf_mkChar(name));
    SET_VECTOR_ELT(out, *i, value);
    (*i)++;
}

static void Rsassy_set_string_feature(SEXP out, SEXP names, R_xlen_t *i, const char *name, const char *value) {
    SEXP sexp_value = PROTECT(Rf_mkString(value == NULL ? "" : value));
    Rsassy_set_feature(out, names, i, name, sexp_value);
    UNPROTECT(1);
}

static void Rsassy_set_logical_feature(SEXP out, SEXP names, R_xlen_t *i, const char *name, int value) {
    SEXP sexp_value = PROTECT(Rf_ScalarLogical(value ? TRUE : FALSE));
    Rsassy_set_feature(out, names, i, name, sexp_value);
    UNPROTECT(1);
}

static void Rsassy_set_backend_feature(SEXP out, SEXP names, R_xlen_t *i, const char *name, const char *csv, int supported_only) {
    SEXP sexp_value = PROTECT(Rsassy_backend_vector(csv, supported_only));
    Rsassy_set_feature(out, names, i, name, sexp_value);
    UNPROTECT(1);
}

static size_t Rsassy_feature_line_count(const char *features) {
    if (features == NULL || features[0] == '\0') {
        return 0;
    }
    size_t n = 0;
    for (const char *p = features; *p; p++) {
        if (*p == '\n') {
            n++;
        }
    }
    if (features[strlen(features) - 1] != '\n') {
        n++;
    }
    return n;
}

static void Rsassy_add_rust_features(SEXP out, SEXP names, R_xlen_t *i, const char *features) {
    if (features == NULL || features[0] == '\0') {
        return;
    }

    char *copy = (char *)R_alloc(strlen(features) + 1, sizeof(char));
    strcpy(copy, features);

    char *line = strtok(copy, "\n");
    while (line != NULL) {
        char *sep = strstr(line, ": ");
        if (sep == NULL) {
            Rsassy_set_string_feature(out, names, i, line, "");
        } else {
            *sep = '\0';
            SEXP value = PROTECT(Rsassy_feature_value(sep + 2));
            Rsassy_set_feature(out, names, i, line, value);
            UNPROTECT(1);
        }
        line = strtok(NULL, "\n");
    }
}

SEXP RC_sassy_features(void) {
    Rsassy_init_backend();

    const char *installed_backends = Rsassy_available_backend_names();
    const char *rust_features = rsassy_features_string();
    R_xlen_t n = (R_xlen_t)(7 + Rsassy_feature_line_count(rust_features));
    SEXP out = PROTECT(Rf_allocVector(VECSXP, n));
    SEXP names = PROTECT(Rf_allocVector(STRSXP, n));
    R_xlen_t i = 0;

    Rsassy_set_string_feature(out, names, &i, "rsassy_dispatch", Rsassy_dispatch_mode());
    Rsassy_set_string_feature(out, names, &i, "rsassy_selected_backend", Rsassy_selected_backend_name());
    Rsassy_set_backend_feature(out, names, &i, "rsassy_installed_backends", installed_backends, 0);
    Rsassy_set_backend_feature(out, names, &i, "rsassy_supported_backends", installed_backends, 1);
    Rsassy_set_logical_feature(out, names, &i, "cpu_avx2", Rsassy_cpu_avx2());
    Rsassy_set_logical_feature(out, names, &i, "cpu_avx512f", Rsassy_cpu_avx512f());
    Rsassy_set_logical_feature(out, names, &i, "cpu_neon", Rsassy_cpu_neon());
    Rsassy_add_rust_features(out, names, &i, rust_features);

    Rf_setAttrib(out, R_NamesSymbol, names);
    UNPROTECT(2);
    return out;
}

static const R_CallMethodDef CallEntries[] = {
    {"RC_sassy_features", (DL_FUNC)&RC_sassy_features, 0},
    {"RC_sassy_set_backend", (DL_FUNC)&RC_sassy_set_backend, 1},
    {"RC_sassy_searcher_new", (DL_FUNC)&RC_sassy_searcher_new, 3},
    {"RC_sassy_searcher_search", (DL_FUNC)&RC_sassy_searcher_search, 8},
    {NULL, NULL, 0}
};

void R_init_Rsassy(DllInfo *dll) {
    R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);
}
