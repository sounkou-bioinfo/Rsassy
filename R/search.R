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
#' @return A data frame with 0-based indices and coordinates: `pattern_idx`, `text_idx`, `text_start`, `text_end`, `pattern_start`, `pattern_end`, `cost`, `strand`, and `cigar`. If requested, also includes `match_region`.
#' @export
sassy_searcher_search <- function(searcher,
                                  pattern,
                                  text,
                                  k,
                                  all = FALSE,
                                  threads = 1L,
                                  mode = "single",
                                  match_region = FALSE) {
  .Call(
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
                         match_region = FALSE) {
  searcher <- sassy_searcher(alphabet = alphabet, rc = rc, alpha = alpha)
  sassy_searcher_search(
    searcher,
    pattern = pattern,
    text = text,
    k = k,
    all = all,
    threads = threads,
    mode = mode,
    match_region = match_region
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
                                    match_region = FALSE) {
  searcher <- sassy_searcher(alphabet = alphabet, rc = rc, alpha = alpha)
  .Call(
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
