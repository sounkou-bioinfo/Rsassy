#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>
#include <R_ext/Altrep.h>

#include "rsassy_native.h"
#include "rsassy_platform.h"

#include <limits.h>
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
    const char *message = Rsassy_native.last_error_message();
    if (message == NULL || message[0] == '\0') {
        message = "unknown Rsassy native error";
    }
    Rf_error("%s", message);
}

static void Rsassy_searcher_finalizer(SEXP xp) {
    RsassySearcher *searcher = (RsassySearcher *)R_ExternalPtrAddr(xp);
    if (searcher != NULL) {
        Rsassy_native.searcher_free(searcher);
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

static double Rsassy_fraction_scalar(SEXP x, const char *arg) {
    if ((TYPEOF(x) != REALSXP && TYPEOF(x) != INTSXP) || XLENGTH(x) != 1) {
        Rf_error("%s must be a numeric scalar in [0, 1]", arg);
    }
    double value = Rf_asReal(x);
    if (!R_FINITE(value) || value < 0 || value > 1) {
        Rf_error("%s must be a numeric scalar in [0, 1]", arg);
    }
    return value;
}

/* ---- FASTX chunked ALTREP batches --------------------------------------
 *
 * Rust/needletail owns iterator parsing and immutable batch slabs.  C owns the
 * R lifetime/ALTREP surface.  Raw sequence/quality ALTREPs expose read-only
 * pointers directly into the batch slab; writable access materializes a private
 * R raw vector copy and leaves the shared slab unchanged.
 */

enum {
    RSASSY_FASTX_BUFFER_SEQ = 0,
    RSASSY_FASTX_BUFFER_QUAL = 1,
    RSASSY_FASTX_RAW_STATE_META = 0,
    RSASSY_FASTX_RAW_STATE_MATERIALIZED = 1,
    RSASSY_FASTX_RAW_STATE_LEN = 2,
};

static R_altrep_class_t Rsassy_fastx_id_class;
static R_altrep_class_t Rsassy_fastx_raw_class;
static R_altrep_class_t Rsassy_fastx_list_class;

static SEXP Rsassy_fastx_iter_tag(void) {
    return Rf_install("Rsassy_fastx_iter_ptr");
}

static SEXP Rsassy_fastx_batch_tag(void) {
    return Rf_install("Rsassy_fastx_batch_ptr");
}

static void Rsassy_fastx_iter_finalizer(SEXP xp) {
    RsassyFastxIter *iter = (RsassyFastxIter *)R_ExternalPtrAddr(xp);
    if (iter != NULL) {
        Rsassy_native.fastx_iter_free(iter);
        R_ClearExternalPtr(xp);
    }
}

static void Rsassy_fastx_batch_finalizer(SEXP xp) {
    RsassyFastxBatch *batch = (RsassyFastxBatch *)R_ExternalPtrAddr(xp);
    if (batch != NULL) {
        Rsassy_native.fastx_batch_free(batch);
        R_ClearExternalPtr(xp);
    }
}

static RsassyFastxIter *Rsassy_fastx_iter_from_xptr(SEXP xp) {
    if (TYPEOF(xp) != EXTPTRSXP || R_ExternalPtrTag(xp) != Rsassy_fastx_iter_tag()) {
        Rf_error("iter must be an external pointer created by sassy_fastx_iter()");
    }
    RsassyFastxIter *iter = (RsassyFastxIter *)R_ExternalPtrAddr(xp);
    if (iter == NULL) {
        Rf_error("FASTX iterator pointer is no longer valid");
    }
    return iter;
}

static RsassyFastxBatch *Rsassy_fastx_batch_from_xptr(SEXP xp) {
    if (TYPEOF(xp) != EXTPTRSXP || R_ExternalPtrTag(xp) != Rsassy_fastx_batch_tag()) {
        Rf_error("FASTX batch owner is invalid");
    }
    RsassyFastxBatch *batch = (RsassyFastxBatch *)R_ExternalPtrAddr(xp);
    if (batch == NULL) {
        Rf_error("FASTX batch pointer is no longer valid");
    }
    return batch;
}

static R_xlen_t Rsassy_fastx_r_length(uintptr_t len, const char *what) {
    if ((uint64_t)len > (uint64_t)R_XLEN_T_MAX) {
        Rf_error("%s is too large for an R vector", what);
    }
    return (R_xlen_t)len;
}

static uintptr_t Rsassy_fastx_batch_n_checked(RsassyFastxBatch *batch) {
    uintptr_t n = Rsassy_native.fastx_batch_n(batch);
    (void)Rsassy_fastx_r_length(n, "FASTX batch");
    return n;
}

static int Rsassy_fastx_kind(SEXP kind_s) {
    if (TYPEOF(kind_s) != INTSXP || XLENGTH(kind_s) != 1 || INTEGER(kind_s)[0] == NA_INTEGER) {
        Rf_error("internal FASTX buffer kind is invalid");
    }
    int kind = INTEGER(kind_s)[0];
    if (kind != RSASSY_FASTX_BUFFER_SEQ && kind != RSASSY_FASTX_BUFFER_QUAL) {
        Rf_error("internal FASTX buffer kind is unknown");
    }
    return kind;
}

static SEXP Rsassy_fastx_make_raw_state(int kind, R_xlen_t index) {
    SEXP state = PROTECT(Rf_allocVector(VECSXP, RSASSY_FASTX_RAW_STATE_LEN));
    SEXP meta = PROTECT(Rf_allocVector(REALSXP, 2));
    REAL(meta)[0] = (double)kind;
    REAL(meta)[1] = (double)index;
    SET_VECTOR_ELT(state, RSASSY_FASTX_RAW_STATE_META, meta);
    SET_VECTOR_ELT(state, RSASSY_FASTX_RAW_STATE_MATERIALIZED, R_NilValue);
    UNPROTECT(2);
    return state;
}

static SEXP Rsassy_fastx_raw_state(SEXP x) {
    SEXP state = R_altrep_data2(x);
    if (TYPEOF(state) != VECSXP || XLENGTH(state) != RSASSY_FASTX_RAW_STATE_LEN) {
        Rf_error("internal FASTX raw ALTREP state is invalid");
    }
    return state;
}

static void Rsassy_fastx_raw_meta(SEXP x, int *kind, uintptr_t *index) {
    SEXP state = Rsassy_fastx_raw_state(x);
    SEXP meta = VECTOR_ELT(state, RSASSY_FASTX_RAW_STATE_META);
    if (TYPEOF(meta) != REALSXP || XLENGTH(meta) != 2) {
        Rf_error("internal FASTX raw ALTREP metadata is invalid");
    }
    double kind_d = REAL(meta)[0];
    double index_d = REAL(meta)[1];
    if (!R_FINITE(kind_d) || !R_FINITE(index_d) || kind_d < 0 || index_d < 0 || floor(kind_d) != kind_d || floor(index_d) != index_d) {
        Rf_error("internal FASTX raw ALTREP metadata is invalid");
    }
    *kind = (int)kind_d;
    if (*kind != RSASSY_FASTX_BUFFER_SEQ && *kind != RSASSY_FASTX_BUFFER_QUAL) {
        Rf_error("internal FASTX raw ALTREP buffer kind is invalid");
    }
    *index = (uintptr_t)index_d;
}

static SEXP Rsassy_fastx_raw_materialized(SEXP x) {
    SEXP state = Rsassy_fastx_raw_state(x);
    SEXP materialized = VECTOR_ELT(state, RSASSY_FASTX_RAW_STATE_MATERIALIZED);
    if (materialized != R_NilValue && TYPEOF(materialized) != RAWSXP) {
        Rf_error("internal FASTX raw materialization is invalid");
    }
    return materialized;
}

static uintptr_t Rsassy_fastx_slice_len(RsassyFastxBatch *batch, int kind, uintptr_t index) {
    return kind == RSASSY_FASTX_BUFFER_SEQ
               ? Rsassy_native.fastx_batch_seq_len(batch, index)
               : Rsassy_native.fastx_batch_qual_len(batch, index);
}

static const uint8_t *Rsassy_fastx_slice_ptr(RsassyFastxBatch *batch, int kind, uintptr_t index) {
    return kind == RSASSY_FASTX_BUFFER_SEQ
               ? Rsassy_native.fastx_batch_seq_ptr(batch, index)
               : Rsassy_native.fastx_batch_qual_ptr(batch, index);
}

static R_xlen_t Rsassy_fastx_raw_Length(SEXP x) {
    RsassyFastxBatch *batch = Rsassy_fastx_batch_from_xptr(R_altrep_data1(x));
    int kind;
    uintptr_t index;
    Rsassy_fastx_raw_meta(x, &kind, &index);
    return Rsassy_fastx_r_length(Rsassy_fastx_slice_len(batch, kind, index), "FASTX raw slice");
}

static Rbyte Rsassy_fastx_raw_Elt(SEXP x, R_xlen_t i) {
    SEXP materialized = Rsassy_fastx_raw_materialized(x);
    if (materialized != R_NilValue) {
        return RAW(materialized)[i];
    }
    RsassyFastxBatch *batch = Rsassy_fastx_batch_from_xptr(R_altrep_data1(x));
    int kind;
    uintptr_t index;
    Rsassy_fastx_raw_meta(x, &kind, &index);
    uintptr_t len = Rsassy_fastx_slice_len(batch, kind, index);
    if (i < 0 || (uint64_t)i >= (uint64_t)len) {
        Rf_error("FASTX raw slice index out of bounds");
    }
    const uint8_t *ptr = Rsassy_fastx_slice_ptr(batch, kind, index);
    if (ptr == NULL && len > 0) {
        Rf_error("FASTX raw slice pointer is unavailable");
    }
    return (Rbyte)ptr[i];
}

static R_xlen_t Rsassy_fastx_raw_Get_region(SEXP x, R_xlen_t i, R_xlen_t n, Rbyte *buf) {
    if (n <= 0) {
        return 0;
    }
    R_xlen_t len = Rsassy_fastx_raw_Length(x);
    if (i < 0 || i >= len) {
        return 0;
    }
    R_xlen_t out_n = n < (len - i) ? n : (len - i);
    SEXP materialized = Rsassy_fastx_raw_materialized(x);
    if (materialized != R_NilValue) {
        memcpy(buf, RAW(materialized) + i, (size_t)out_n);
        return out_n;
    }
    RsassyFastxBatch *batch = Rsassy_fastx_batch_from_xptr(R_altrep_data1(x));
    int kind;
    uintptr_t index;
    Rsassy_fastx_raw_meta(x, &kind, &index);
    const uint8_t *ptr = Rsassy_fastx_slice_ptr(batch, kind, index);
    if (ptr == NULL && len > 0) {
        Rf_error("FASTX raw slice pointer is unavailable");
    }
    memcpy(buf, ptr + i, (size_t)out_n);
    return out_n;
}

static const void *Rsassy_fastx_raw_Dataptr_or_null(SEXP x) {
    SEXP materialized = Rsassy_fastx_raw_materialized(x);
    if (materialized != R_NilValue) {
        return RAW(materialized);
    }
    RsassyFastxBatch *batch = Rsassy_fastx_batch_from_xptr(R_altrep_data1(x));
    int kind;
    uintptr_t index;
    Rsassy_fastx_raw_meta(x, &kind, &index);
    uintptr_t len = Rsassy_fastx_slice_len(batch, kind, index);
    if (len == 0) {
        return NULL;
    }
    return Rsassy_fastx_slice_ptr(batch, kind, index);
}

static void *Rsassy_fastx_raw_Dataptr(SEXP x, Rboolean writeable) {
    SEXP materialized = Rsassy_fastx_raw_materialized(x);
    if (materialized != R_NilValue) {
        return RAW(materialized);
    }
    R_xlen_t len = Rsassy_fastx_raw_Length(x);
    const void *ptr = Rsassy_fastx_raw_Dataptr_or_null(x);
    if (!writeable) {
        return (void *)ptr;
    }

    SEXP state = Rsassy_fastx_raw_state(x);
    SEXP copy = PROTECT(Rf_allocVector(RAWSXP, len));
    if (len > 0) {
        if (ptr == NULL) {
            Rf_error("FASTX raw slice pointer is unavailable");
        }
        memcpy(RAW(copy), ptr, (size_t)len);
    }
    SET_VECTOR_ELT(state, RSASSY_FASTX_RAW_STATE_MATERIALIZED, copy);
    UNPROTECT(1);
    return RAW(copy);
}

static R_xlen_t Rsassy_fastx_id_Length(SEXP x) {
    RsassyFastxBatch *batch = Rsassy_fastx_batch_from_xptr(R_altrep_data1(x));
    return Rsassy_fastx_r_length(Rsassy_fastx_batch_n_checked(batch), "FASTX id vector");
}

static SEXP Rsassy_fastx_id_Elt(SEXP x, R_xlen_t i) {
    RsassyFastxBatch *batch = Rsassy_fastx_batch_from_xptr(R_altrep_data1(x));
    uintptr_t n = Rsassy_fastx_batch_n_checked(batch);
    if (i < 0 || (uint64_t)i >= (uint64_t)n) {
        Rf_error("FASTX id index out of bounds");
    }
    uintptr_t len = Rsassy_native.fastx_batch_id_len(batch, (uintptr_t)i);
    if (len > (uintptr_t)INT_MAX) {
        Rf_error("FASTX id is too long for an R character string");
    }
    if (len == 0) {
        return R_BlankString;
    }
    const uint8_t *ptr = Rsassy_native.fastx_batch_id_ptr(batch, (uintptr_t)i);
    if (ptr == NULL && len > 0) {
        Rf_error("FASTX id pointer is unavailable");
    }
    cetype_t encoding = Rsassy_native.fastx_batch_id_utf8(batch, (uintptr_t)i) ? CE_UTF8 : CE_BYTES;
    return Rf_mkCharLenCE((const char *)ptr, (int)len, encoding);
}

static int Rsassy_fastx_id_No_NA(SEXP x) {
    (void)x;
    return 1;
}

static R_xlen_t Rsassy_fastx_list_Length(SEXP x) {
    RsassyFastxBatch *batch = Rsassy_fastx_batch_from_xptr(R_altrep_data1(x));
    return Rsassy_fastx_r_length(Rsassy_fastx_batch_n_checked(batch), "FASTX sequence list");
}

static SEXP Rsassy_fastx_make_raw_slice(SEXP batch_xp, int kind, R_xlen_t index) {
    SEXP state = PROTECT(Rsassy_fastx_make_raw_state(kind, index));
    SEXP out = R_new_altrep(Rsassy_fastx_raw_class, batch_xp, state);
    UNPROTECT(1);
    return out;
}

static SEXP Rsassy_fastx_list_Elt(SEXP x, R_xlen_t i) {
    RsassyFastxBatch *batch = Rsassy_fastx_batch_from_xptr(R_altrep_data1(x));
    uintptr_t n = Rsassy_fastx_batch_n_checked(batch);
    if (i < 0 || (uint64_t)i >= (uint64_t)n) {
        Rf_error("FASTX list index out of bounds");
    }
    int kind = Rsassy_fastx_kind(R_altrep_data2(x));
    return Rsassy_fastx_make_raw_slice(R_altrep_data1(x), kind, i);
}

static void Rsassy_fastx_list_Set_elt(SEXP x, R_xlen_t i, SEXP v) {
    (void)x;
    (void)i;
    (void)v;
    Rf_error("FASTX ALTREP sequence lists are read-only");
}

static SEXP Rsassy_fastx_make_list_view(SEXP batch_xp, int kind) {
    SEXP kind_s = PROTECT(Rf_ScalarInteger(kind));
    SEXP out = R_new_altrep(Rsassy_fastx_list_class, batch_xp, kind_s);
    UNPROTECT(1);
    return out;
}

static SEXP Rsassy_fastx_make_batch(RsassyFastxBatch *batch) {
    SEXP batch_xp = PROTECT(R_MakeExternalPtr(batch, Rsassy_fastx_batch_tag(), R_NilValue));
    R_RegisterCFinalizerEx(batch_xp, Rsassy_fastx_batch_finalizer, TRUE);

    SEXP id = PROTECT(R_new_altrep(Rsassy_fastx_id_class, batch_xp, R_NilValue));
    SEXP seq = PROTECT(Rsassy_fastx_make_list_view(batch_xp, RSASSY_FASTX_BUFFER_SEQ));
    SEXP qual = PROTECT(Rsassy_native.fastx_batch_has_qual(batch) ? Rsassy_fastx_make_list_view(batch_xp, RSASSY_FASTX_BUFFER_QUAL) : R_NilValue);

    SEXP out = PROTECT(Rf_allocVector(VECSXP, 3));
    SEXP names = PROTECT(Rf_allocVector(STRSXP, 3));
    SET_STRING_ELT(names, 0, Rf_mkChar("id"));
    SET_STRING_ELT(names, 1, Rf_mkChar("seq"));
    SET_STRING_ELT(names, 2, Rf_mkChar("qual"));
    SET_VECTOR_ELT(out, 0, id);
    SET_VECTOR_ELT(out, 1, seq);
    SET_VECTOR_ELT(out, 2, qual);
    Rf_setAttrib(out, R_NamesSymbol, names);

    SEXP klass = PROTECT(Rf_mkString("sassy_fastx_batch"));
    Rf_setAttrib(out, R_ClassSymbol, klass);

    UNPROTECT(7);
    return out;
}

SEXP RC_sassy_fastx_iter_new(SEXP path_s, SEXP batch_records_s, SEXP include_qual_s) {
    Rsassy_init_backend();
    const char *path = Rsassy_string_scalar(path_s, "path");
    uintptr_t batch_records = Rsassy_uintptr_scalar(batch_records_s, "batch_records", 1);
    int include_qual = Rsassy_logical_scalar(include_qual_s, "include_qual");

    RsassyFastxIter *iter = NULL;
    if (Rsassy_native.fastx_iter_new(path, batch_records, include_qual == TRUE, &iter) != 0) {
        Rsassy_stop_last_error();
    }

    SEXP xp = PROTECT(R_MakeExternalPtr(iter, Rsassy_fastx_iter_tag(), R_NilValue));
    R_RegisterCFinalizerEx(xp, Rsassy_fastx_iter_finalizer, TRUE);
    SEXP klass = PROTECT(Rf_mkString("sassy_fastx_iter"));
    Rf_setAttrib(xp, R_ClassSymbol, klass);
    UNPROTECT(2);
    return xp;
}

SEXP RC_sassy_fastx_next(SEXP iter_s) {
    RsassyFastxIter *iter = Rsassy_fastx_iter_from_xptr(iter_s);
    RsassyFastxBatch *batch = NULL;
    if (Rsassy_native.fastx_iter_next(iter, &batch) != 0) {
        Rsassy_stop_last_error();
    }
    if (batch == NULL) {
        return R_NilValue;
    }
    return Rsassy_fastx_make_batch(batch);
}

SEXP RC_sassy_searcher_new(SEXP alphabet_s, SEXP rc_s, SEXP alpha_s) {
    Rsassy_init_backend();
    const char *alphabet = Rsassy_string_scalar(alphabet_s, "alphabet");
    int rc = Rsassy_logical_scalar(rc_s, "rc");
    double alpha = Rsassy_alpha_scalar(alpha_s);
    float alpha_f = isnan(alpha) ? NAN : (float)alpha;

    RsassySearcher *searcher = NULL;
    if (Rsassy_native.searcher_new(alphabet, rc == TRUE, alpha_f, &searcher) != 0) {
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

static void Rsassy_assert_sequence_list(SEXP x, const char *arg) {
    if (TYPEOF(x) != VECSXP) {
        Rf_error("%s must be a list of raw vectors or non-missing character scalars", arg);
    }
}

static struct RsassySeqViews Rsassy_sequences_view(SEXP x, const char *arg) {
    struct RsassySeqViews views;
    views.data = NULL;
    views.len = NULL;
    views.n = 0;

    Rsassy_assert_sequence_list(x, arg);

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

    Rf_error("%s must be a list of raw vectors or non-missing character scalars", arg);
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

static int Rsassy_validate_id_vector(SEXP ids, uintptr_t n, const char *arg, const char *unit) {
    if (ids == R_NilValue) {
        return 0;
    }
    if (TYPEOF(ids) != STRSXP || XLENGTH(ids) != (R_xlen_t)n) {
        Rf_error("%s must be NULL or a character vector with one entry per %s", arg, unit);
    }
    for (R_xlen_t i = 0; i < XLENGTH(ids); i++) {
        if (STRING_ELT(ids, i) == NA_STRING) {
            Rf_error("%s must not contain NA values", arg);
        }
    }
    return 1;
}

static void Rsassy_set_data_frame_attrib(SEXP out, uintptr_t n, int nprotect, int sassy_matches) {
    SEXP row_names = PROTECT(Rf_allocVector(INTSXP, 2)); nprotect++;
    INTEGER(row_names)[0] = NA_INTEGER;
    INTEGER(row_names)[1] = -(int)n;
    Rf_setAttrib(out, R_RowNamesSymbol, row_names);

    SEXP klass = PROTECT(Rf_allocVector(STRSXP, sassy_matches ? 2 : 1)); nprotect++;
    if (sassy_matches) {
        SET_STRING_ELT(klass, 0, Rf_mkChar("sassy_matches"));
        SET_STRING_ELT(klass, 1, Rf_mkChar("data.frame"));
    } else {
        SET_STRING_ELT(klass, 0, Rf_mkChar("data.frame"));
    }
    Rf_setAttrib(out, R_ClassSymbol, klass);

    UNPROTECT(nprotect);
}

static SEXP Rsassy_matches_data_frame(RsassyMatch *matches,
                                      uintptr_t n,
                                      bool include_match_region,
                                      SEXP pattern_id_s,
                                      uintptr_t n_patterns,
                                      SEXP text_id_s,
                                      uintptr_t n_texts) {
    if ((uint64_t)n > (uint64_t)INT_MAX) {
        Rf_error("too many matches to return as an R data frame");
    }
    int has_pattern_id = Rsassy_validate_id_vector(pattern_id_s, n_patterns, "pattern_id", "pattern");
    int has_text_id = Rsassy_validate_id_vector(text_id_s, n_texts, "text_id", "text");
    if (n > 1) {
        qsort(matches, (size_t)n, sizeof(RsassyMatch), Rsassy_match_compare_input_order);
    }

    int ncol = 9 + (include_match_region ? 1 : 0) + has_pattern_id + has_text_id;
    int nprotect = 0;
    R_xlen_t rn = (R_xlen_t)n;
    SEXP pattern_idx = PROTECT(Rf_allocVector(INTSXP, rn)); nprotect++;
    SEXP pattern_id = has_pattern_id ? PROTECT(Rf_allocVector(STRSXP, rn)) : R_NilValue;
    if (has_pattern_id) nprotect++;
    SEXP text_idx = PROTECT(Rf_allocVector(INTSXP, rn)); nprotect++;
    SEXP text_id = has_text_id ? PROTECT(Rf_allocVector(STRSXP, rn)) : R_NilValue;
    if (has_text_id) nprotect++;
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
        if (matches[i].pattern_idx >= n_patterns || matches[i].text_idx >= n_texts) {
            Rf_error("match pattern_idx/text_idx are outside input bounds");
        }
        INTEGER(pattern_idx)[i] = (int)matches[i].pattern_idx;
        if (has_pattern_id) {
            SET_STRING_ELT(pattern_id, i, STRING_ELT(pattern_id_s, (R_xlen_t)matches[i].pattern_idx));
        }
        INTEGER(text_idx)[i] = (int)matches[i].text_idx;
        if (has_text_id) {
            SET_STRING_ELT(text_id, i, STRING_ELT(text_id_s, (R_xlen_t)matches[i].text_idx));
        }
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
    SEXP names = PROTECT(Rf_allocVector(STRSXP, ncol)); nprotect++;
    int col = 0;
#define RSASSY_SET_COL(name, value) do { \
        SET_VECTOR_ELT(out, col, value); \
        SET_STRING_ELT(names, col, Rf_mkChar(name)); \
        col++; \
    } while (0)
    RSASSY_SET_COL("pattern_idx", pattern_idx);
    if (has_pattern_id) RSASSY_SET_COL("pattern_id", pattern_id);
    RSASSY_SET_COL("text_idx", text_idx);
    if (has_text_id) RSASSY_SET_COL("text_id", text_id);
    RSASSY_SET_COL("text_start", text_start);
    RSASSY_SET_COL("text_end", text_end);
    RSASSY_SET_COL("pattern_start", pattern_start);
    RSASSY_SET_COL("pattern_end", pattern_end);
    RSASSY_SET_COL("cost", cost);
    RSASSY_SET_COL("strand", strand);
    RSASSY_SET_COL("cigar", cigar);
    if (include_match_region) RSASSY_SET_COL("match_region", match_region);
#undef RSASSY_SET_COL
    Rf_setAttrib(out, R_NamesSymbol, names);
    Rsassy_set_data_frame_attrib(out, n, nprotect, 1);
    return out;
}

static SEXP Rsassy_crispr_data_frame(RsassyMatch *matches,
                                     uintptr_t n,
                                     const struct RsassySeqViews *guides,
                                     SEXP pattern_id_s,
                                     const struct RsassySeqViews *texts,
                                     SEXP text_id_s) {
    if ((uint64_t)n > (uint64_t)INT_MAX) {
        Rf_error("too many matches to return as an R data frame");
    }
    int has_pattern_id = Rsassy_validate_id_vector(pattern_id_s, guides->n, "pattern_id", "guide");
    int has_text_id = Rsassy_validate_id_vector(text_id_s, texts->n, "text_id", "text");
    if (n > 1) {
        qsort(matches, (size_t)n, sizeof(RsassyMatch), Rsassy_match_compare_input_order);
    }

    int ncol = 7 + has_pattern_id + has_text_id;
    int nprotect = 0;
    R_xlen_t rn = (R_xlen_t)n;
    SEXP pattern_id = has_pattern_id ? PROTECT(Rf_allocVector(STRSXP, rn)) : R_NilValue;
    if (has_pattern_id) nprotect++;
    SEXP guide = PROTECT(Rf_allocVector(STRSXP, rn)); nprotect++;
    SEXP text_id = has_text_id ? PROTECT(Rf_allocVector(STRSXP, rn)) : R_NilValue;
    if (has_text_id) nprotect++;
    SEXP cost = PROTECT(Rf_allocVector(INTSXP, rn)); nprotect++;
    SEXP strand = PROTECT(Rf_allocVector(STRSXP, rn)); nprotect++;
    SEXP start = PROTECT(Rf_allocVector(REALSXP, rn)); nprotect++;
    SEXP end = PROTECT(Rf_allocVector(REALSXP, rn)); nprotect++;
    SEXP match_region = PROTECT(Rf_allocVector(STRSXP, rn)); nprotect++;
    SEXP cigar = PROTECT(Rf_allocVector(STRSXP, rn)); nprotect++;

    for (R_xlen_t i = 0; i < rn; i++) {
        if (matches[i].pattern_idx >= guides->n || matches[i].text_idx >= texts->n) {
            Rf_error("match pattern_idx/text_idx are outside input bounds");
        }
        if (has_pattern_id) {
            SET_STRING_ELT(pattern_id, i, STRING_ELT(pattern_id_s, (R_xlen_t)matches[i].pattern_idx));
        }
        if ((uint64_t)guides->len[matches[i].pattern_idx] > (uint64_t)INT_MAX ||
            (uint64_t)matches[i].match_region_len > (uint64_t)INT_MAX) {
            Rf_error("guide or match_region is too large to return as an R string");
        }
        SET_STRING_ELT(guide, i, Rf_mkCharLenCE((const char *)guides->data[matches[i].pattern_idx],
                                                (int)guides->len[matches[i].pattern_idx], CE_BYTES));
        if (has_text_id) {
            SET_STRING_ELT(text_id, i, STRING_ELT(text_id_s, (R_xlen_t)matches[i].text_idx));
        }
        INTEGER(cost)[i] = (int)matches[i].cost;
        SET_STRING_ELT(strand, i, Rf_mkChar(matches[i].strand == 0 ? "+" : "-"));
        REAL(start)[i] = (double)matches[i].text_start;
        REAL(end)[i] = (double)matches[i].text_end;
        const char *region = matches[i].match_region == NULL ? "" : matches[i].match_region;
        SET_STRING_ELT(match_region, i, Rf_mkCharLenCE(region, (int)matches[i].match_region_len, CE_BYTES));
        SET_STRING_ELT(cigar, i, Rf_mkChar(matches[i].cigar == NULL ? "" : matches[i].cigar));
    }

    SEXP out = PROTECT(Rf_allocVector(VECSXP, ncol)); nprotect++;
    SEXP names = PROTECT(Rf_allocVector(STRSXP, ncol)); nprotect++;
    int col = 0;
#define RSASSY_SET_COL(name, value) do { \
        SET_VECTOR_ELT(out, col, value); \
        SET_STRING_ELT(names, col, Rf_mkChar(name)); \
        col++; \
    } while (0)
    if (has_pattern_id) RSASSY_SET_COL("pattern_id", pattern_id);
    RSASSY_SET_COL("guide", guide);
    if (has_text_id) RSASSY_SET_COL("text_id", text_id);
    RSASSY_SET_COL("cost", cost);
    RSASSY_SET_COL("strand", strand);
    RSASSY_SET_COL("start", start);
    RSASSY_SET_COL("end", end);
    RSASSY_SET_COL("match_region", match_region);
    RSASSY_SET_COL("cigar", cigar);
#undef RSASSY_SET_COL
    Rf_setAttrib(out, R_NamesSymbol, names);
    Rsassy_set_data_frame_attrib(out, n, nprotect, 0);
    return out;
}

SEXP RC_sassy_searcher_search(SEXP searcher_s,
                              SEXP pattern_s,
                              SEXP text_s,
                              SEXP k_s,
                              SEXP all_s,
                              SEXP threads_s,
                              SEXP strategy_s,
                              SEXP match_region_s,
                              SEXP pattern_id_s,
                              SEXP text_id_s) {
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
        if (Rsassy_native.searcher_search(searcher,
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
        if (Rsassy_native.searcher_search_many(searcher,
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

    SEXP out = Rsassy_matches_data_frame(matches,
                                         n_matches,
                                         include_match_region == TRUE,
                                         pattern_id_s,
                                         patterns.n,
                                         text_id_s,
                                         texts.n);
    Rsassy_native.matches_free(matches, n_matches);
    return out;
}

SEXP RC_sassy_crispr(SEXP guide_s,
                     SEXP text_s,
                     SEXP k_s,
                     SEXP pam_length_s,
                     SEXP allow_pam_edits_s,
                     SEXP max_n_frac_s,
                     SEXP rc_s,
                     SEXP threads_s,
                     SEXP pattern_id_s,
                     SEXP text_id_s) {
    Rsassy_init_backend();
    struct RsassySeqViews guides = Rsassy_sequences_view(guide_s, "guide");
    struct RsassySeqViews texts = Rsassy_sequences_view(text_s, "text");

    uintptr_t k = Rsassy_uintptr_scalar(k_s, "k", 0);
    uintptr_t pam_length = Rsassy_uintptr_scalar(pam_length_s, "pam_length", 1);
    int allow_pam_edits = Rsassy_logical_scalar(allow_pam_edits_s, "allow_pam_edits");
    double max_n_frac = Rsassy_fraction_scalar(max_n_frac_s, "max_n_frac");
    int rc = Rsassy_logical_scalar(rc_s, "rc");
    uintptr_t threads = Rsassy_uintptr_scalar(threads_s, "threads", 1);

    RsassyMatch *matches = NULL;
    uintptr_t n_matches = 0;
    if (Rsassy_native.crispr_search_many(guides.data,
                                  guides.len,
                                  guides.n,
                                  texts.data,
                                  texts.len,
                                  texts.n,
                                  k,
                                  pam_length,
                                  allow_pam_edits == TRUE,
                                  (float)max_n_frac,
                                  rc == TRUE,
                                  threads,
                                  &matches,
                                  &n_matches) != 0) {
        Rsassy_stop_last_error();
    }

    SEXP out = Rsassy_crispr_data_frame(matches,
                                        n_matches,
                                        &guides,
                                        pattern_id_s,
                                        &texts,
                                        text_id_s);
    Rsassy_native.matches_free(matches, n_matches);
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
    const char *rust_features = Rsassy_native.features_string();
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
    {"RC_sassy_fastx_iter_new", (DL_FUNC)&RC_sassy_fastx_iter_new, 3},
    {"RC_sassy_fastx_next", (DL_FUNC)&RC_sassy_fastx_next, 1},
    {"RC_sassy_searcher_new", (DL_FUNC)&RC_sassy_searcher_new, 3},
    {"RC_sassy_searcher_search", (DL_FUNC)&RC_sassy_searcher_search, 10},
    {"RC_sassy_crispr", (DL_FUNC)&RC_sassy_crispr, 10},
    {NULL, NULL, 0}
};

void R_init_Rsassy(DllInfo *dll) {
    R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);

    Rsassy_fastx_id_class = R_make_altstring_class("sassy_fastx_id", "Rsassy", dll);
    R_set_altrep_Length_method(Rsassy_fastx_id_class, Rsassy_fastx_id_Length);
    R_set_altstring_Elt_method(Rsassy_fastx_id_class, Rsassy_fastx_id_Elt);
    R_set_altstring_No_NA_method(Rsassy_fastx_id_class, Rsassy_fastx_id_No_NA);

    Rsassy_fastx_raw_class = R_make_altraw_class("sassy_fastx_raw", "Rsassy", dll);
    R_set_altrep_Length_method(Rsassy_fastx_raw_class, Rsassy_fastx_raw_Length);
    R_set_altraw_Elt_method(Rsassy_fastx_raw_class, Rsassy_fastx_raw_Elt);
    R_set_altraw_Get_region_method(Rsassy_fastx_raw_class, Rsassy_fastx_raw_Get_region);
    R_set_altvec_Dataptr_or_null_method(Rsassy_fastx_raw_class, Rsassy_fastx_raw_Dataptr_or_null);
    R_set_altvec_Dataptr_method(Rsassy_fastx_raw_class, Rsassy_fastx_raw_Dataptr);

    Rsassy_fastx_list_class = R_make_altlist_class("sassy_fastx_list", "Rsassy", dll);
    R_set_altrep_Length_method(Rsassy_fastx_list_class, Rsassy_fastx_list_Length);
    R_set_altlist_Elt_method(Rsassy_fastx_list_class, Rsassy_fastx_list_Elt);
    R_set_altlist_Set_elt_method(Rsassy_fastx_list_class, Rsassy_fastx_list_Set_elt);
}
