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
#' sassy_searcher_search(searcher, "ACGT", "TTACGTAA", 0)
#' @export
sassy_searcher <- function(alphabet = "dna", rc = TRUE, alpha = NULL) {
  .Call("RC_sassy_searcher_new", alphabet, rc, alpha, PACKAGE = "Rsassy")
}

#' Search with a reusable 'sassy' searcher
#'
#' `pattern` and `text` may be single sequences or vectors/lists of sequences.
#' When vectors are supplied, every pattern is searched against every text and
#' the returned `pattern_idx` and `text_idx` columns identify the 0-based input
#' indices. Use `threads > 1` for larger batches.
#'
#' @param searcher A searcher created by [sassy_searcher()].
#' @param pattern,text Raw vectors, character vectors, or lists of raw vectors / character scalars.
#' @param k Maximum edit distance.
#' @param all If `FALSE`, return local-minimum matches. If `TRUE`, return all end positions with score <= `k`.
#' @param threads Number of worker threads to request for bulk searches.
#' @param mode Bulk search mode. `"single"` searches each pair independently; `"batch_texts"` uses one text per SIMD lane. `"batch_patterns"` and `"encoded_patterns"` (alias `"v2"`) use Sassy's multi-pattern encoding, which in `sassy` 0.2.1 is implemented for `alphabet = "iupac"` and equal byte-length patterns.
#' @param match_region If `TRUE`, include a `match_region` column. Reverse-strand
#'   regions are reverse-complemented so the region and CIGAR are in the input
#'   pattern direction.
#' @param sam If `TRUE`, format reverse-strand `match_region` and `cigar` in the
#'   text direction used by SAM and by the upstream `sassy --sam` output.
#' @return A data frame with 0-based indices and coordinates: `pattern_idx`, `text_idx`, `text_start`, `text_end`, `pattern_start`, `pattern_end`, `cost`, `strand`, and `cigar`. If requested, also includes `match_region`.
#' @export
sassy_searcher_search <- function(searcher,
                                  pattern,
                                  text,
                                  k,
                                  all = FALSE,
                                  threads = 1L,
                                  mode = "single",
                                  match_region = FALSE,
                                  sam = FALSE) {
  out <- .Call(
    "RC_sassy_searcher_search",
    searcher,
    pattern,
    text,
    k,
    all,
    threads,
    mode,
    match_region,
    PACKAGE = "Rsassy"
  )
  if (sassy_logical_scalar(sam, "sam")) {
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
#' @return A data frame with 0-based indices and coordinates: `pattern_idx`, `text_idx`, `text_start`, `text_end`, `pattern_start`, `pattern_end`, `cost`, `strand`, and `cigar`. If requested, also includes `match_region`.
#' @examples
#' sassy_search("ACGT", "TTACGTAA", 0, alphabet = "dna", rc = FALSE)
#' @export
sassy_search <- function(pattern,
                         text,
                         k,
                         alphabet = "dna",
                         rc = TRUE,
                         alpha = NULL,
                         all = FALSE,
                         threads = 1L,
                         mode = "single",
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
    mode = mode,
    match_region = match_region,
    sam = sam
  )
}

#' Search an R connection with 'sassy'
#'
#' Streams bytes from an already-open readable R connection through the C/R API
#' boundary. This avoids an R-level `readBin()`/`readChar()` loop while still
#' preserving matches that cross chunk boundaries by keeping an overlap window.
#' `pattern` may be a single sequence or a vector/list of sequences, with
#' `pattern_idx` identifying the 0-based input pattern index.
#'
#' @inheritParams sassy_search
#' @param pattern Raw vector, character vector, or list of raw vectors / character scalars.
#' @param con An open readable R connection, preferably opened in binary mode.
#' @param chunk_size Number of new bytes to read per native chunk.
#' @param overlap Number of bytes to carry from one chunk to the next. If `NULL`, C computes a default from the longest pattern, `k`, and `alpha`.
#' @return A data frame of matches with coordinates relative to the full stream.
#' @export
sassy_search_connection <- function(pattern,
                                    con,
                                    k,
                                    alphabet = "dna",
                                    rc = TRUE,
                                    alpha = NULL,
                                    all = FALSE,
                                    threads = 1L,
                                    mode = "single",
                                    chunk_size = 1024 * 1024,
                                    overlap = NULL,
                                    match_region = FALSE,
                                    sam = FALSE) {
  searcher <- sassy_searcher(alphabet = alphabet, rc = rc, alpha = alpha)
  out <- .Call(
    "RC_sassy_searcher_search_connection",
    searcher,
    pattern,
    con,
    k,
    all,
    threads,
    mode,
    chunk_size,
    overlap,
    match_region,
    PACKAGE = "Rsassy"
  )
  if (sassy_logical_scalar(sam, "sam")) {
    out <- sassy_as_sam(out, alphabet = alphabet)
  }
  out
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
#'   sassy_search("ACGA", "TTTCGTTT", 0, alphabet = "dna", match_region = TRUE),
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
#' @param guide Character vector of guide sequences including the PAM suffix.
#' @param text Text sequences to search; a character vector, raw vector, or list
#'   of raw/character scalars accepted by [sassy_search()].
#' @param k Maximum edit distance for the searched guide sequence. With
#'   `allow_pam_edits = FALSE`, the exact-PAM filter means this is effectively
#'   the edit threshold outside the PAM.
#' @param pam_length Length of the PAM suffix.
#' @param allow_pam_edits If `TRUE`, do not require an exact PAM match.
#' @param max_n_frac Maximum allowed fraction of `N` bases in `match_region`.
#' @param rc If `TRUE`, search reverse-complement targets as well.
#' @param threads Number of worker threads to request.
#' @param text_id Optional text identifiers. Defaults to names on `text` when
#'   all names are non-empty, otherwise `text_1`, `text_2`, ...
#' @return A data frame with CLI-style columns: `guide`, `text_id`, `cost`,
#'   `strand`, `start`, `end`, `match_region`, and `cigar`.
#' @examples
#' sassy_crispr("ACGTNGG", c(chr1 = "TTTACGTAGGTTT"), k = 0, rc = FALSE)
#' @export
sassy_crispr <- function(guide,
                         text,
                         k,
                         pam_length = 3L,
                         allow_pam_edits = FALSE,
                         max_n_frac = 0.2,
                         rc = TRUE,
                         threads = 1L,
                         text_id = NULL) {
  guide <- sassy_character_vector(guide, "guide")
  pam_length <- sassy_whole_number(pam_length, "pam_length", min = 1L)
  allow_pam_edits <- sassy_logical_scalar(allow_pam_edits, "allow_pam_edits")
  rc <- sassy_logical_scalar(rc, "rc")
  max_n_frac <- sassy_fraction_scalar(max_n_frac, "max_n_frac")
  text_ids <- sassy_text_ids(text, text_id)

  guide_len <- nchar(guide, type = "bytes", allowNA = FALSE, keepNA = FALSE)
  if (any(guide_len < pam_length)) {
    stop("all guide sequences must be at least pam_length bytes long", call. = FALSE)
  }
  pam <- substring(guide, guide_len - pam_length + 1L, guide_len)
  if (length(unique(pam)) != 1L) {
    stop("all guide sequences must have the same PAM suffix", call. = FALSE)
  }

  matches <- sassy_search(
    pattern = guide,
    text = text,
    k = k,
    alphabet = "iupac",
    rc = rc,
    all = TRUE,
    threads = threads,
    mode = "single",
    match_region = TRUE,
    sam = FALSE
  )
  if (nrow(matches) == 0L) {
    return(sassy_empty_crispr_matches())
  }

  keep <- rep(TRUE, nrow(matches))
  if (!allow_pam_edits) {
    region_pam <- sassy_suffix(matches$match_region, pam_length)
    keep <- keep & sassy_iupac_matches(region_pam, pam[matches$pattern_idx + 1L])
  }
  keep <- keep & sassy_n_fraction(matches$match_region) <= max_n_frac
  matches <- matches[keep, , drop = FALSE]
  if (nrow(matches) == 0L) {
    return(sassy_empty_crispr_matches())
  }

  row.names(matches) <- NULL
  data.frame(
    guide = guide[matches$pattern_idx + 1L],
    text_id = text_ids[matches$text_idx + 1L],
    cost = matches$cost,
    strand = matches$strand,
    start = matches$text_start,
    end = matches$text_end,
    match_region = matches$match_region,
    cigar = matches$cigar,
    stringsAsFactors = FALSE
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

sassy_iupac_matches <- function(query, target) {
  vapply(seq_along(query), function(i) {
    query_mask <- sassy_iupac_mask(query[[i]])
    target_mask <- sassy_iupac_mask(target[[i]])
    !is.null(query_mask) &&
      !is.null(target_mask) &&
      length(query_mask) == length(target_mask) &&
      all(bitwAnd(query_mask, target_mask) > 0L)
  }, logical(1), USE.NAMES = FALSE)
}

sassy_iupac_mask <- function(x) {
  map <- c(
    A = 1L, C = 2L, T = 4L, U = 4L, G = 8L,
    N = 15L, R = 9L, Y = 6L, S = 10L, W = 5L,
    K = 12L, M = 3L, B = 14L, D = 13L, H = 7L,
    V = 11L, X = 0L
  )
  chars <- toupper(strsplit(x, "", useBytes = TRUE)[[1]])
  mask <- unname(map[chars])
  if (anyNA(mask)) {
    return(NULL)
  }
  mask
}

sassy_suffix <- function(x, n) {
  len <- nchar(x, type = "bytes", allowNA = FALSE, keepNA = FALSE)
  substring(x, pmax(1L, len - n + 1L), len)
}

sassy_n_fraction <- function(x) {
  len <- nchar(x, type = "bytes", allowNA = FALSE, keepNA = FALSE)
  n_count <- lengths(regmatches(x, gregexpr("[Nn]", x, perl = TRUE)))
  ifelse(len == 0L, 0, n_count / len)
}

sassy_character_vector <- function(x, arg) {
  if (!is.character(x) || anyNA(x)) {
    stop(sprintf("%s must be a character vector without NA values", arg), call. = FALSE)
  }
  if (length(x) == 0L) {
    stop(sprintf("%s must not be empty", arg), call. = FALSE)
  }
  x
}

sassy_logical_scalar <- function(x, arg) {
  if (!is.logical(x) || length(x) != 1L || is.na(x)) {
    stop(sprintf("%s must be TRUE or FALSE", arg), call. = FALSE)
  }
  isTRUE(x)
}

sassy_alphabet_scalar <- function(x) {
  if (!is.character(x) || length(x) != 1L || is.na(x)) {
    stop("alphabet must be a non-missing character scalar", call. = FALSE)
  }
  tolower(x)
}

sassy_whole_number <- function(x, arg, min = 0L) {
  if (!is.numeric(x) || length(x) != 1L || !is.finite(x) || x != floor(x) || x < min) {
    stop(sprintf("%s must be a whole number >= %d", arg, min), call. = FALSE)
  }
  as.integer(x)
}

sassy_fraction_scalar <- function(x, arg) {
  if (!is.numeric(x) || length(x) != 1L || !is.finite(x) || x < 0 || x > 1) {
    stop(sprintf("%s must be a number in [0, 1]", arg), call. = FALSE)
  }
  as.numeric(x)
}

sassy_sequence_count <- function(x, arg) {
  if (is.raw(x)) {
    return(1L)
  }
  if (is.character(x) || is.list(x)) {
    return(length(x))
  }
  stop(sprintf("%s must be a raw vector, character vector, or list", arg), call. = FALSE)
}

sassy_text_ids <- function(text, text_id = NULL) {
  n <- sassy_sequence_count(text, "text")
  if (!is.null(text_id)) {
    if (!is.character(text_id) || length(text_id) != n || anyNA(text_id)) {
      stop("text_id must be NULL or a character vector with one entry per text", call. = FALSE)
    }
    return(text_id)
  }
  if (!is.raw(text)) {
    text_names <- names(text)
    if (!is.null(text_names) && length(text_names) == n && all(nzchar(text_names))) {
      return(text_names)
    }
  }
  paste0("text_", seq_len(n))
}

sassy_empty_crispr_matches <- function() {
  data.frame(
    guide = character(),
    text_id = character(),
    cost = integer(),
    strand = character(),
    start = numeric(),
    end = numeric(),
    match_region = character(),
    cigar = character(),
    stringsAsFactors = FALSE
  )
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
