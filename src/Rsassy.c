#define _GNU_SOURCE

#include <R.h>
#include <R_ext/Rdynload.h>
#include <Rinternals.h>

#include "rsassy_native.h"
/* R_ext/Connections.h is R's experimental connection API. Rsassy keeps all
 * connection access isolated in this file so higher-level R code does not need
 * readBin()/readChar() loops for streaming input. */
#include <R_ext/Connections.h>

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

static uintptr_t Rsassy_max_pattern_len(const struct RsassySeqViews *patterns) {
    uintptr_t out = 0;
    for (uintptr_t i = 0; i < patterns->n; i++) {
        if (patterns->len[i] > out) {
            out = patterns->len[i];
        }
    }
    return out;
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

static bool Rsassy_mode_is_encoded(const char *mode) {
    return strcmp(mode, "encoded_patterns") == 0 || strcmp(mode, "v2") == 0;
}

static bool Rsassy_mode_requires_equal_pattern_lengths(const char *mode) {
    return strcmp(mode, "batch_patterns") == 0 || Rsassy_mode_is_encoded(mode);
}

static void Rsassy_check_equal_pattern_lengths(const struct RsassySeqViews *patterns, const char *mode) {
    if (!Rsassy_mode_requires_equal_pattern_lengths(mode) || patterns->n <= 1) {
        return;
    }
    uintptr_t len0 = patterns->len[0];
    for (uintptr_t i = 1; i < patterns->n; i++) {
        if (patterns->len[i] != len0) {
            Rf_error("mode = '%s' requires all patterns to have the same byte length", mode);
        }
    }
}

struct RsassyMatchVec {
    RsassyMatch *data;
    uintptr_t len;
    uintptr_t cap;
};

static char *Rsassy_strdup(const char *x) {
    if (x == NULL) {
        x = "";
    }
    size_t len = strlen(x);
    char *out = (char *)malloc(len + 1);
    if (out == NULL) {
        Rf_error("failed to allocate CIGAR string");
    }
    memcpy(out, x, len + 1);
    return out;
}

static char *Rsassy_memdup(const char *x, uintptr_t len) {
    char *out = (char *)malloc((size_t)(len == 0 ? 1 : len));
    if (out == NULL) {
        Rf_error("failed to allocate match_region string");
    }
    if (len > 0 && x != NULL) {
        memcpy(out, x, (size_t)len);
    }
    return out;
}

static void Rsassy_match_vec_free(struct RsassyMatchVec *vec) {
    for (uintptr_t i = 0; i < vec->len; i++) {
        free(vec->data[i].cigar);
        vec->data[i].cigar = NULL;
        free(vec->data[i].match_region);
        vec->data[i].match_region = NULL;
        vec->data[i].match_region_len = 0;
    }
    free(vec->data);
    vec->data = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void Rsassy_match_vec_push(struct RsassyMatchVec *vec, RsassyMatch match) {
    if (vec->len == vec->cap) {
        uintptr_t new_cap = vec->cap == 0 ? 64 : vec->cap * 2;
        RsassyMatch *new_data = (RsassyMatch *)realloc(vec->data, (size_t)new_cap * sizeof(RsassyMatch));
        if (new_data == NULL) {
            free(match.cigar);
            free(match.match_region);
            Rsassy_match_vec_free(vec);
            Rf_error("failed to allocate match accumulator");
        }
        vec->data = new_data;
        vec->cap = new_cap;
    }
    vec->data[vec->len++] = match;
}

static SEXP Rsassy_matches_data_frame(const RsassyMatch *matches, uintptr_t n, bool include_match_region) {
    if ((uint64_t)n > (uint64_t)INT_MAX) {
        Rf_error("too many matches to return as an R data frame");
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
                              SEXP mode_s,
                              SEXP match_region_s) {
    Rsassy_init_backend();
    RsassySearcher *searcher = Rsassy_searcher_from_xptr(searcher_s);

    uintptr_t k = Rsassy_uintptr_scalar(k_s, "k", 0);
    int all = Rsassy_logical_scalar(all_s, "all");
    uintptr_t threads = Rsassy_uintptr_scalar(threads_s, "threads", 1);
    const char *mode = Rsassy_string_scalar(mode_s, "mode");
    int include_match_region = Rsassy_logical_scalar(match_region_s, "match_region");

    struct RsassySeqViews patterns = Rsassy_sequences_view(pattern_s, "pattern");
    struct RsassySeqViews texts = Rsassy_sequences_view(text_s, "text");
    Rsassy_check_equal_pattern_lengths(&patterns, mode);

    RsassyMatch *matches = NULL;
    uintptr_t n_matches = 0;
    if (patterns.n == 1 && texts.n == 1 && threads == 1 && !Rsassy_mode_is_encoded(mode)) {
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
                                        mode,
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

SEXP RC_sassy_searcher_search_connection(SEXP searcher_s,
                                         SEXP pattern_s,
                                         SEXP connection_s,
                                         SEXP k_s,
                                         SEXP all_s,
                                         SEXP threads_s,
                                         SEXP mode_s,
                                         SEXP chunk_size_s,
                                         SEXP overlap_s,
                                         SEXP match_region_s) {
    Rsassy_init_backend();
    RsassySearcher *searcher = Rsassy_searcher_from_xptr(searcher_s);
    struct RsassySeqViews patterns = Rsassy_sequences_view(pattern_s, "pattern");

    uintptr_t k = Rsassy_uintptr_scalar(k_s, "k", 0);
    int all = Rsassy_logical_scalar(all_s, "all");
    uintptr_t threads = Rsassy_uintptr_scalar(threads_s, "threads", 1);
    const char *mode = Rsassy_string_scalar(mode_s, "mode");
    int include_match_region = Rsassy_logical_scalar(match_region_s, "match_region");
    Rsassy_check_equal_pattern_lengths(&patterns, mode);

    uintptr_t chunk_size = Rsassy_uintptr_scalar(chunk_size_s, "chunk_size", 1);
    SEXP alpha_attr = Rf_getAttrib(searcher_s, Rf_install("alpha"));
    double alpha = alpha_attr == R_NilValue ? NAN : Rf_asReal(alpha_attr);
    uintptr_t max_pattern_len = Rsassy_max_pattern_len(&patterns);
    uintptr_t overlap;
    if (overlap_s == R_NilValue) {
        uintptr_t multiplier = isnan(alpha) ? 1 : 2;
        if (max_pattern_len > (SIZE_MAX - k) / multiplier) {
            Rf_error("default overlap is too large for this platform");
        }
        overlap = multiplier * max_pattern_len + k;
    } else {
        overlap = Rsassy_uintptr_scalar(overlap_s, "overlap", 0);
    }
    if (overlap > SIZE_MAX - chunk_size) {
        Rf_error("chunk_size + overlap is too large for this platform");
    }

    Rconnection con = R_GetConnection(connection_s);
    if (con == NULL) {
        Rf_error("con must be an R connection");
    }
    if (!con->isopen || !con->canread) {
        Rf_error("con must be an open readable connection, preferably opened in binary mode");
    }

    uint8_t *window = (uint8_t *)malloc(chunk_size + overlap);
    if (window == NULL) {
        Rf_error("failed to allocate streaming buffer");
    }

    struct RsassyMatchVec acc = {0};
    size_t carry_len = 0;
    uintptr_t bytes_read_total = 0;

    for (;;) {
        size_t n_read = R_ReadConnection(con, window + carry_len, chunk_size);
        if (n_read == 0) {
            break;
        }

        uintptr_t chunk_start = bytes_read_total - (uintptr_t)carry_len;
        uintptr_t new_bytes_start = bytes_read_total;
        uintptr_t chunk_len = (uintptr_t)(carry_len + n_read);
        bytes_read_total += (uintptr_t)n_read;

        RsassyMatch *matches = NULL;
        uintptr_t n_matches = 0;
        int status;
        if (patterns.n == 1 && threads == 1 && !Rsassy_mode_is_encoded(mode)) {
            status = rsassy_searcher_search(searcher,
                                            patterns.data[0],
                                            patterns.len[0],
                                            window,
                                            chunk_len,
                                            k,
                                            all == TRUE,
                                            include_match_region == TRUE,
                                            &matches,
                                            &n_matches);
        } else {
            const uint8_t *texts[1] = {window};
            uintptr_t text_lens[1] = {chunk_len};
            status = rsassy_searcher_search_many(searcher,
                                                 patterns.data,
                                                 patterns.len,
                                                 patterns.n,
                                                 texts,
                                                 text_lens,
                                                 1,
                                                 k,
                                                 all == TRUE,
                                                 threads,
                                                 mode,
                                                 include_match_region == TRUE,
                                                 &matches,
                                                 &n_matches);
        }
        if (status != 0) {
            free(window);
            Rsassy_match_vec_free(&acc);
            Rsassy_stop_last_error();
        }

        for (uintptr_t i = 0; i < n_matches; i++) {
            RsassyMatch match = matches[i];
            uintptr_t global_end = chunk_start + match.text_end;
            if (global_end <= new_bytes_start) {
                continue;
            }
            match.text_start += chunk_start;
            match.text_end = global_end;
            match.cigar = Rsassy_strdup(matches[i].cigar);
            if (include_match_region == TRUE) {
                match.match_region = Rsassy_memdup(matches[i].match_region, matches[i].match_region_len);
                match.match_region_len = matches[i].match_region_len;
            }
            Rsassy_match_vec_push(&acc, match);
        }
        rsassy_matches_free(matches, n_matches);

        carry_len = overlap < (size_t)chunk_len ? overlap : (size_t)chunk_len;
        if (carry_len > 0) {
            memmove(window, window + chunk_len - carry_len, carry_len);
        }
    }

    free(window);
    SEXP out = Rsassy_matches_data_frame(acc.data, acc.len, include_match_region == TRUE);
    Rsassy_match_vec_free(&acc);
    return out;
}

static const R_CallMethodDef CallEntries[] = {
    {"RC_sassy_features", (DL_FUNC)&RC_sassy_features, 0},
    {"RC_sassy_set_backend", (DL_FUNC)&RC_sassy_set_backend, 1},
    {"RC_sassy_searcher_new", (DL_FUNC)&RC_sassy_searcher_new, 3},
    {"RC_sassy_searcher_search", (DL_FUNC)&RC_sassy_searcher_search, 8},
    {"RC_sassy_searcher_search_connection", (DL_FUNC)&RC_sassy_searcher_search_connection, 10},
    {NULL, NULL, 0}
};

void R_init_Rsassy(DllInfo *dll) {
    R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);
}
