#' @useDynLib Rsassy, .registration = TRUE
NULL

#' Report Rsassy build and CPU feature information
#'
#' Returns diagnostic information about the selected Rsassy backend. Calling
#' this initializes the native backend if it has not already been loaded.
#' `rsassy_selected_backend` reports the runtime-selected backend.
#' `rsassy_installed_backends` is a character vector of backend libraries found
#' in the package installation, and `rsassy_supported_backends` is the subset
#' supported by the current CPU/runtime. With `"auto"` selection, Rsassy chooses
#' the best supported installed backend: AVX-512 before AVX2 on x86_64, NEON on
#' arm64, WebAssembly SIMD128 on wasm, and scalar otherwise. The `selected_*`
#' fields describe the loaded Rust backend. The `cpu_*` fields are detected by
#' the C shim.
#'
#' @return A `sassy_features` list of build, selected-backend, and
#'   CPU/runtime feature values.
#' @examples
#' sassy_features()
#' @export
sassy_features <- function() {
  features <- .Call("RC_sassy_features", PACKAGE = "Rsassy")
  class(features) <- c("sassy_features", class(features))
  features
}

#' Print Rsassy feature information
#'
#' @param x A `sassy_features` object returned by [sassy_features()].
#' @param ... Ignored; accepted for compatibility with [print()].
#' @return `x`, invisibly.
#' @export
print.sassy_features <- function(x, ...) {
  cat("<sassy_features>\n")
  cat("dispatch: ", sassy_feature_scalar(x$rsassy_dispatch), "\n", sep = "")
  cat("selected backend: ", sassy_feature_scalar(x$rsassy_selected_backend), "\n", sep = "")
  cat("installed backends: ", sassy_feature_collapse(x$rsassy_installed_backends), "\n", sep = "")
  cat("supported backends: ", sassy_feature_collapse(x$rsassy_supported_backends), "\n", sep = "")
  cat("CPU: avx2=", sassy_feature_bool(x$cpu_avx2),
    " avx512f=", sassy_feature_bool(x$cpu_avx512f),
    " neon=", sassy_feature_bool(x$cpu_neon), "\n",
    sep = ""
  )
  cat("Rust backend: ", sassy_feature_scalar(x$selected_simd_backend),
    " (native_simd=", sassy_feature_bool(x$selected_native_simd), ")\n",
    sep = ""
  )
  invisible(x)
}

sassy_feature_scalar <- function(x) {
  if (is.null(x) || length(x) == 0L || is.na(x[[1L]])) {
    return("<unknown>")
  }
  as.character(x[[1L]])
}

sassy_feature_collapse <- function(x) {
  if (is.null(x) || length(x) == 0L) {
    return("<none>")
  }
  paste(as.character(x), collapse = ", ")
}

sassy_feature_bool <- function(x) {
  if (is.null(x) || length(x) == 0L || is.na(x[[1L]])) {
    return("?")
  }
  if (isTRUE(x[[1L]])) "yes" else "no"
}

#' Select the Rsassy native backend
#'
#' Select a backend for the current R process. Backend loading is intentionally
#' one-shot: the selected shared library is fixed for the lifetime of the R
#' process. This must be called before the first native Rsassy operation,
#' including [sassy_features()], [sassy_searcher()], or [sassy_search()]. Rsassy
#' does not unload and replace backend DLLs because that is not reliable across R
#' platforms. Use this for benchmarking installed backends against each other in
#' separate fresh R processes.
#'
#' @param backend One of `"auto"`, `"scalar"`, `"avx2"`, `"avx512"`,
#'   `"neon"`, or `"wasm_simd128"`.
#' @return The requested backend name, invisibly. `"auto"` means runtime dispatch
#'   will choose the best installed backend supported by the current CPU/runtime
#'   when the backend is first loaded.
#' @export
sassy_set_backend <- function(backend = c("auto", "scalar", "avx2", "avx512", "neon", "wasm_simd128")) {
  backend <- match.arg(backend)
  invisible(.Call("RC_sassy_set_backend", backend, PACKAGE = "Rsassy"))
}

#' Create a chunked FASTA/FASTQ iterator
#'
#' `sassy_fastx_iter()` opens a FASTA or FASTQ file and returns an iterator that
#' yields record-count-bounded batches. Parsing is performed by the vendored
#' Rust `needletail` parser. Sequence and quality data in each batch are exposed
#' as read-only raw ALTREP slices over immutable native batch buffers; they are
#' not eagerly materialized as R strings.
#'
#' @param path Path to a FASTA/FASTQ file. Gzip-compressed input is supported by
#'   the vendored `needletail` gzip backend.
#' @param batch_records Maximum number of records returned by each
#'   `sassy_fastx_next()` call.
#' @param include_qual If `TRUE`, FASTQ qualities are included as `batch$qual`.
#'   If `FALSE`, or for FASTA input, `batch$qual` is `NULL`.
#' @return An external pointer with class `sassy_fastx_iter`.
#' @examples
#' fq <- tempfile(fileext = ".fastq")
#' writeLines(c("@r1", "ACGT", "+", "!!!!"), fq, useBytes = TRUE)
#' it <- sassy_fastx_iter(fq, batch_records = 1)
#' batch <- sassy_fastx_next(it)
#' rawToChar(batch$seq[[1]])
#' @export
sassy_fastx_iter <- function(path, batch_records = 100000L, include_qual = TRUE) {
  if (!is.character(path) || length(path) != 1L || is.na(path)) {
    stop("path must be a non-missing character scalar", call. = FALSE)
  }
  path <- enc2utf8(path)
  batch_records <- sassy_positive_count_scalar(batch_records, "batch_records")
  include_qual <- sassy_logical_scalar(include_qual, "include_qual")
  .Call("RC_sassy_fastx_iter_new", path, batch_records, include_qual, PACKAGE = "Rsassy")
}

#' Get the next FASTA/FASTQ batch
#'
#' @param iter An iterator created by `sassy_fastx_iter()`.
#' @return `NULL` at end of file, otherwise a `sassy_fastx_batch` list with
#'   `id`, `seq`, and `qual` elements. `id` is an ALTREP character vector, while
#'   `seq` and `qual` are ALTREP lists whose elements are raw ALTREP vectors.
#' @examples
#' fq <- tempfile(fileext = ".fastq")
#' writeLines(c("@r1", "ACGT", "+", "!!!!"), fq, useBytes = TRUE)
#' it <- sassy_fastx_iter(fq, batch_records = 1)
#' batch <- sassy_fastx_next(it)
#' length(batch$id)
#' @export
sassy_fastx_next <- function(iter) {
  .Call("RC_sassy_fastx_next", iter, PACKAGE = "Rsassy")
}

#' Create a reusable 'sassy' searcher
#'
#' A searcher stores the selected alphabet profile and reverse-complement
#' behavior. Reuse a searcher when searching many patterns or texts with the
#' same settings.
#'
#' @param alphabet Alphabet profile. One of `"dna"`, `"iupac"`, or `"ascii"`.
#' @param rc If `TRUE`, search reverse-complement strand as well where supported.
#' @param alpha Optional IUPAC overhang cost in `[0, 1]`. Use `NULL` to disable.
#' @return An external pointer with class `sassy_searcher`.
#' @examples
#' searcher <- sassy_searcher("dna", rc = FALSE)
#' sassy_searcher_search(searcher, list("ACGT"), list("TTACGTAA"), 0)
#' @export
sassy_searcher <- function(alphabet = "dna", rc = TRUE, alpha = NULL) {
  .Call("RC_sassy_searcher_new", alphabet, rc, alpha, PACKAGE = "Rsassy")
}

#' Search with a reusable 'sassy' searcher
#'
#' `pattern` and `text` must be lists of sequences. Each element must be a raw
#' vector or a non-missing character scalar. Every pattern is searched against
#' every text and the returned `pattern_idx` and `text_idx` columns identify the
#' 0-based input indices. Use `threads > 1` for larger batches.
#'
#' @param searcher A searcher created by [sassy_searcher()].
#' @param pattern List of raw vectors or non-missing character scalars.
#' @param text List of raw vectors or non-missing character scalars.
#' @param pattern_id Optional pattern identifiers. If supplied, must be a
#'   non-missing character vector with one entry per pattern and adds/replaces a
#'   `pattern_id` column. Names on `pattern` are not inspected.
#' @param text_id Optional text identifiers. If supplied, must be a non-missing
#'   character vector with one entry per text and adds/replaces a `text_id`
#'   column. Names on `text` are not inspected.
#' @param k Maximum edit distance.
#' @param all If `FALSE`, return the usual local-minimum matches. If `TRUE`,
#'   return every end position with score <= `k`; this can include overlapping
#'   and nested candidate alignments and requires `strategy = "pairwise"`.
#' @param threads Number of worker threads to request for bulk searches.
#' @param strategy Search strategy. `"pairwise"` searches each pattern/text pair
#'   independently and is the general default. `"batch_texts"` uses one text per
#'   SIMD lane. `"batch_patterns"` and `"encoded_patterns"` (alias `"v2"`) use
#'   Sassy's multi-pattern encoding, which in `sassy` 0.2.1 is implemented for
#'   `alphabet = "iupac"` and equal byte-length patterns.
#' @param match_region If `TRUE`, include a `match_region` column. Reverse-strand
#'   regions are reverse-complemented so the region and CIGAR are in the input
#'   pattern direction.
#' @param sam If `TRUE`, format reverse-strand `match_region` and `cigar` in the
#'   text direction used by SAM and by the upstream `sassy --sam` output.
#' @return A data frame with 0-based indices and coordinates: `pattern_idx`, `text_idx`, `text_start`, `text_end`, `pattern_start`, `pattern_end`, `cost`, `strand`, and `cigar`. If `pattern_id` or `text_id` are supplied, mapped identifier columns are included. If requested, also includes `match_region`. Rows are ordered by input text, then text start/end coordinate, then pattern index.
#' @export
sassy_searcher_search <- function(searcher,
                                  pattern,
                                  text,
                                  k,
                                  all = FALSE,
                                  threads = 1L,
                                  strategy = "pairwise",
                                  pattern_id = NULL,
                                  text_id = NULL,
                                  match_region = FALSE,
                                  sam = FALSE) {
  strategy <- sassy_strategy_scalar(strategy)
  all <- sassy_logical_scalar(all, "all")
  sam <- sassy_logical_scalar(sam, "sam")
  sassy_check_all_strategy(all, strategy)
  out <- .Call(
    "RC_sassy_searcher_search",
    searcher,
    pattern,
    text,
    k,
    all,
    threads,
    strategy,
    match_region,
    pattern_id,
    text_id,
    PACKAGE = "Rsassy"
  )
  if (sam) {
    out <- sassy_as_sam(out, alphabet = attr(searcher, "alphabet", exact = TRUE))
  }
  out
}

#' Search approximate matches with 'sassy'
#'
#' Convenience wrapper that creates a searcher, searches, and returns a
#' `sassy_matches` data frame. Coordinates are 0-based and half-open.
#'
#' @inheritParams sassy_searcher
#' @inheritParams sassy_searcher_search
#' @return A data frame with 0-based indices and coordinates: `pattern_idx`, `text_idx`, `text_start`, `text_end`, `pattern_start`, `pattern_end`, `cost`, `strand`, and `cigar`. If `pattern_id` or `text_id` are supplied, mapped identifier columns are included. If requested, also includes `match_region`. Rows are ordered by input text, then text start/end coordinate, then pattern index.
#' @examples
#' sassy_search(list("ACGT"), list("TTACGTAA"), 0, alphabet = "dna", rc = FALSE)
#' @export
sassy_search <- function(pattern,
                         text,
                         k,
                         alphabet = "dna",
                         rc = TRUE,
                         alpha = NULL,
                         all = FALSE,
                         threads = 1L,
                         strategy = "pairwise",
                         pattern_id = NULL,
                         text_id = NULL,
                         match_region = FALSE,
                         sam = FALSE) {
  searcher <- sassy_searcher(alphabet = alphabet, rc = rc, alpha = alpha)
  sassy_searcher_search(
    searcher,
    pattern = pattern,
    text = text,
    k = k,
    all = all,
    threads = threads,
    strategy = strategy,
    pattern_id = pattern_id,
    text_id = text_id,
    match_region = match_region,
    sam = sam
  )
}

#' Format matches in SAM-compatible text direction
#'
#' Rsassy normally follows the upstream `sassy` TSV convention: reverse-strand
#' `match_region` values are reverse-complemented and CIGAR strings are oriented
#' in the input pattern direction. `sassy_as_sam()` converts reverse-strand rows
#' to the text direction used by SAM and by upstream `sassy --sam` output.
#'
#' @param x A `sassy_matches` data frame.
#' @param alphabet Alphabet profile used for the search. One of `"dna"` or
#'   `"iupac"` when `x` includes `match_region`.
#' @return A copy of `x` with reverse-strand `cigar` values reversed and, when
#'   present, reverse-strand `match_region` values reverse-complemented back to
#'   text direction.
#' @examples
#' sassy_as_sam(
#'   sassy_search(list("ACGA"), list("TTTCGTTT"), 0, alphabet = "dna", match_region = TRUE),
#'   alphabet = "dna"
#' )
#' @export
sassy_as_sam <- function(x, alphabet = "dna") {
  if (!is.data.frame(x)) {
    stop("x must be a data frame returned by Rsassy", call. = FALSE)
  }
  if (!all(c("strand", "cigar") %in% names(x))) {
    stop("x must contain strand and cigar columns", call. = FALSE)
  }

  out <- x
  rc <- !is.na(out$strand) & out$strand == "-"
  if (!any(rc)) {
    return(out)
  }

  out$cigar[rc] <- sassy_reverse_cigar(out$cigar[rc])
  if ("match_region" %in% names(out)) {
    alphabet <- sassy_alphabet_scalar(alphabet)
    if (!alphabet %in% c("dna", "iupac")) {
      stop("SAM match_region formatting requires alphabet = 'dna' or 'iupac'", call. = FALSE)
    }
    out$match_region[rc] <- sassy_reverse_complement(out$match_region[rc])
  }
  out
}

#' Search CRISPR guide targets
#'
#' `sassy_crispr()` is an R-level equivalent of the upstream `sassy crispr`
#' workflow for in-memory sequences. Guides include the PAM at the end. By
#' default, the PAM must match exactly under IUPAC matching, while the rest of
#' the guide may have up to `k` edits.
#'
#' @param guide List of guide sequences including the PAM suffix. Each element
#'   must be a raw vector or non-missing character scalar.
#' @param text List of text sequences to search. Each element must be a raw
#'   vector or non-missing character scalar.
#' @param k Maximum edit distance for the searched guide sequence. With
#'   `allow_pam_edits = FALSE`, the exact-PAM filter means this is effectively
#'   the edit threshold outside the PAM.
#' @param pam_length Length of the PAM suffix.
#' @param allow_pam_edits If `TRUE`, do not require an exact PAM match.
#' @param max_n_frac Maximum allowed fraction of `N` bases in `match_region`.
#' @param rc If `TRUE`, search reverse-complement targets as well.
#' @param threads Number of worker threads to request.
#' @param pattern_id Optional guide/pattern identifiers. If supplied, must be a
#'   character vector with one entry per guide and adds/replaces a `pattern_id`
#'   column. Names on `guide` are not inspected.
#' @param text_id Optional text identifiers. If supplied, must be a character
#'   vector with one entry per text and adds/replaces a `text_id` column. Names
#'   on `text` are not inspected.
#' @return A data frame with CLI-style columns: `guide`, `cost`, `strand`,
#'   `start`, `end`, `match_region`, and `cigar`. If `pattern_id` or `text_id`
#'   are supplied, mapped identifier columns are included.
#' @examples
#' sassy_crispr(list("ACGTNGG"), list("TTTACGTAGGTTT"), k = 0, rc = FALSE, text_id = "chr1")
#' @export
sassy_crispr <- function(guide,
                         text,
                         k,
                         pam_length = 3L,
                         allow_pam_edits = FALSE,
                         max_n_frac = 0.2,
                         rc = TRUE,
                         threads = 1L,
                         pattern_id = NULL,
                         text_id = NULL) {
  .Call(
    "RC_sassy_crispr",
    guide,
    text,
    k,
    pam_length,
    allow_pam_edits,
    max_n_frac,
    rc,
    threads,
    pattern_id,
    text_id,
    PACKAGE = "Rsassy"
  )
}

sassy_reverse_cigar <- function(cigar) {
  vapply(cigar, function(one) {
    if (is.na(one)) {
      return(NA_character_)
    }
    matches <- gregexpr("[0-9]+[A-Z=]", one, perl = TRUE)[[1]]
    if (matches[[1]] == -1L) {
      return(one)
    }
    tokens <- regmatches(one, list(matches))[[1]]
    if (!identical(paste0(tokens, collapse = ""), one)) {
      return(one)
    }
    paste0(rev(tokens), collapse = "")
  }, character(1), USE.NAMES = FALSE)
}

sassy_reverse_complement <- function(x) {
  comp <- chartr(
    "ACGTRYSWKMBDHVNXacgtryswkmbdhvnx",
    "TGCAYRSWMKVHDBNXtgcayrswmkvhdbnx",
    x
  )
  vapply(strsplit(comp, "", useBytes = TRUE), function(chars) {
    paste0(rev(chars), collapse = "")
  }, character(1), USE.NAMES = FALSE)
}

sassy_logical_scalar <- function(x, arg) {
  if (!is.logical(x) || length(x) != 1L || is.na(x)) {
    stop(sprintf("%s must be TRUE or FALSE", arg), call. = FALSE)
  }
  isTRUE(x)
}

sassy_positive_count_scalar <- function(x, arg) {
  if (!is.numeric(x) || length(x) != 1L || is.na(x) || !is.finite(x) || x < 1 || x != floor(x)) {
    stop(sprintf("%s must be a positive integer-like scalar", arg), call. = FALSE)
  }
  x
}

sassy_alphabet_scalar <- function(x) {
  if (!is.character(x) || length(x) != 1L || is.na(x)) {
    stop("alphabet must be a non-missing character scalar", call. = FALSE)
  }
  tolower(x)
}

sassy_strategy_scalar <- function(x) {
  if (!is.character(x) || length(x) != 1L || is.na(x)) {
    stop("strategy must be a non-missing character scalar", call. = FALSE)
  }
  allowed <- c("pairwise", "batch_texts", "batch_patterns", "encoded_patterns", "v2")
  if (!x %in% allowed) {
    stop(
      "strategy must be one of 'pairwise', 'batch_texts', 'batch_patterns', 'encoded_patterns', or 'v2'",
      call. = FALSE
    )
  }
  x
}

sassy_check_all_strategy <- function(all, strategy) {
  if (isTRUE(all) && !identical(strategy, "pairwise")) {
    stop(
      "all = TRUE maps to sassy::Searcher::search_all() and requires strategy = 'pairwise'",
      call. = FALSE
    )
  }
  invisible(NULL)
}

#' Print sassy match data frames
#'
#' @param x A `sassy_matches` data frame.
#' @param ... Ignored; accepted for compatibility with [print()].
#' @param color If `TRUE`, color `match_region` by CIGAR operation with ANSI
#'   escape sequences: green matches, orange substitutions, blue inserted text,
#'   and red gaps for pattern bases absent from the text. Defaults to
#'   `getOption("Rsassy.coloring", FALSE)`.
#' @return `x`, invisibly.
#' @export
print.sassy_matches <- function(x, ..., color = getOption("Rsassy.coloring", FALSE)) {
  cat(sprintf(
    "<sassy_matches> %d match%s\n",
    nrow(x),
    if (nrow(x) == 1L) "" else "es"
  ))

  out <- as.data.frame(x)
  if (all(c("cigar", "match_region") %in% names(out)) && isTRUE(color)) {
    out$match_region <- sassy_color_match_region(out$match_region, out$cigar)
    sassy_print_data_frame(out)
  } else {
    sassy_print_data_frame(out)
  }
  invisible(x)
}

sassy_print_data_frame <- function(x) {
  if (ncol(x) == 0L) {
    cat(sprintf("data frame with 0 columns and %d rows\n", nrow(x)))
    return(invisible(NULL))
  }

  formatted <- lapply(x, function(col) {
    if (is.character(col)) {
      format(col, justify = "right", na.encode = TRUE)
    } else {
      format(col, trim = TRUE, scientific = FALSE)
    }
  })
  widths <- pmax(
    nchar(names(x), type = "width", allowNA = FALSE),
    vapply(formatted, function(col) {
      visible <- sassy_strip_ansi(col)
      if (length(visible) == 0L) 0L else max(nchar(visible, type = "width", allowNA = FALSE))
    }, integer(1))
  )

  cat(paste(vapply(seq_along(formatted), function(j) {
    sassy_pad_left(names(x)[[j]], widths[[j]])
  }, character(1)), collapse = " "), "\n", sep = "")

  for (i in seq_len(nrow(x))) {
    cat(paste(vapply(seq_along(formatted), function(j) {
      sassy_pad_left(formatted[[j]][[i]], widths[[j]])
    }, character(1)), collapse = " "), "\n", sep = "")
  }
  invisible(NULL)
}

sassy_strip_ansi <- function(x) {
  gsub("\033\\[[0-9;]*m", "", x, perl = TRUE)
}

sassy_pad_left <- function(x, width) {
  visible_width <- nchar(sassy_strip_ansi(x), type = "width", allowNA = FALSE)
  paste0(strrep(" ", pmax(0L, width - visible_width)), x)
}

sassy_color_match_region <- function(x, cigar) {
  vapply(seq_along(x), function(i) {
    sassy_color_one_match_region(x[[i]], cigar[[i]])
  }, character(1), USE.NAMES = FALSE)
}

sassy_color_one_match_region <- function(region, cigar) {
  if (is.na(region) || is.na(cigar)) {
    return(region)
  }

  matches <- gregexpr("[0-9]+[=MXID]", cigar, perl = TRUE)[[1]]
  if (matches[[1]] == -1L) {
    return(region)
  }
  tokens <- regmatches(cigar, list(matches))[[1]]
  if (!identical(paste0(tokens, collapse = ""), cigar)) {
    return(region)
  }

  chars <- strsplit(region, "", useBytes = TRUE)[[1]]
  pos <- 1L
  out <- character()
  for (token in tokens) {
    count <- as.integer(sub("^([0-9]+).*$", "\\1", token, perl = TRUE))
    op <- sub("^[0-9]+", "", token, perl = TRUE)
    if (is.na(count) || count < 0L) {
      return(region)
    }

    if (op %in% c("=", "M", "X", "D")) {
      end <- pos + count - 1L
      if (count > 0L && (pos < 1L || end > length(chars))) {
        return(region)
      }
      piece <- if (count == 0L) character() else chars[pos:end]
      pos <- end + 1L
      code <- switch(op,
        "=" = "32",
        M = "32",
        X = "38;5;208",
        D = "34"
      )
      out <- c(out, sassy_ansi(piece, code))
    } else if (op == "I") {
      out <- c(out, sassy_ansi(rep("-", count), "31"))
    } else {
      return(region)
    }
  }

  if (pos != length(chars) + 1L) {
    return(region)
  }
  paste0(out, collapse = "")
}

sassy_ansi <- function(x, code) {
  if (length(x) == 0L) {
    return(character())
  }
  paste0("\033[", code, "m", x, "\033[39m")
}
