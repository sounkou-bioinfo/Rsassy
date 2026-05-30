#!/usr/bin/env Rscript

args <- commandArgs(trailingOnly = TRUE)
sassy_bin <- if (length(args)) args[[1L]] else Sys.which("sassy")
if (!nzchar(sassy_bin)) {
  stop("Could not find the sassy CLI. Pass its path as the first argument.", call. = FALSE)
}
if (!file.exists(sassy_bin) && !nzchar(Sys.which(sassy_bin))) {
  stop("sassy CLI does not exist: ", sassy_bin, call. = FALSE)
}

library(Rsassy)

run_cli <- function(args, stdout = TRUE) {
  out_file <- tempfile("sassy-cli-stdout-")
  err_file <- tempfile("sassy-cli-stderr-")
  on.exit(unlink(c(out_file, err_file)), add = TRUE)

  status <- system2(sassy_bin, args, stdout = out_file, stderr = err_file)
  out <- readLines(out_file, warn = FALSE)
  err <- readLines(err_file, warn = FALSE)
  if (!identical(status, 0L)) {
    cat("sassy CLI stdout:\n", paste(out, collapse = "\n"), "\n", sep = "")
    cat("sassy CLI stderr:\n", paste(err, collapse = "\n"), "\n", sep = "")
    stop("sassy CLI failed with status ", status, call. = FALSE)
  }
  if (stdout) out else invisible(out)
}

read_tsv_lines <- function(lines) {
  utils::read.delim(
    text = paste(lines, collapse = "\n"),
    stringsAsFactors = FALSE,
    check.names = FALSE
  )
}

write_fasta <- function(records, file) {
  stopifnot(!is.null(names(records)), all(nzchar(names(records))))
  con <- file(file, open = "wb")
  on.exit(close(con), add = TRUE)
  for (id in names(records)) {
    writeLines(c(paste0(">", id), records[[id]]), con = con, sep = "\n", useBytes = TRUE)
  }
  invisible(file)
}

search_to_cli <- function(x, pattern_ids, text_ids) {
  out <- data.frame(
    pat_id = pattern_ids[x$pattern_idx + 1L],
    text_id = text_ids[x$text_idx + 1L],
    cost = as.integer(x$cost),
    strand = as.character(x$strand),
    start = as.integer(x$text_start),
    end = as.integer(x$text_end),
    match_region = as.character(x$match_region),
    cigar = as.character(x$cigar),
    stringsAsFactors = FALSE
  )
  normalize_cli(out)
}

normalize_cli <- function(x) {
  cols <- c("pat_id", "text_id", "cost", "strand", "start", "end", "match_region", "cigar")
  x <- as.data.frame(x)[cols]
  x$cost <- as.integer(x$cost)
  x$start <- as.integer(x$start)
  x$end <- as.integer(x$end)
  x$pat_id <- as.character(x$pat_id)
  x$text_id <- as.character(x$text_id)
  x$strand <- as.character(x$strand)
  x$match_region <- as.character(x$match_region)
  x$cigar <- as.character(x$cigar)
  if (nrow(x)) {
    x <- x[do.call(order, x), , drop = FALSE]
  }
  row.names(x) <- NULL
  x
}

normalize_crispr <- function(x) {
  cols <- c("guide", "text_id", "cost", "strand", "start", "end", "match_region", "cigar")
  x <- as.data.frame(x)[cols]
  x$cost <- as.integer(x$cost)
  x$start <- as.integer(x$start)
  x$end <- as.integer(x$end)
  for (col in setdiff(cols, c("cost", "start", "end"))) {
    x[[col]] <- as.character(x[[col]])
  }
  if (nrow(x)) {
    x <- x[do.call(order, x), , drop = FALSE]
  }
  row.names(x) <- NULL
  x
}

expect_identical_df <- function(label, r, cli) {
  if (!identical(r, cli)) {
    cat("R result:\n")
    print(r)
    cat("CLI result:\n")
    print(cli)
    stop("CLI conformance mismatch: ", label, call. = FALSE)
  }
  message("PASS ", label)
}

version <- run_cli("--version")
message("Using ", paste(version, collapse = " "))
if (!any(grepl("0\\.2\\.1", version))) {
  stop("Expected sassy CLI version 0.2.1", call. = FALSE)
}

work <- tempfile("rsassy-cli-conformance-")
dir.create(work)
on.exit(unlink(work, recursive = TRUE), add = TRUE)

# Search TSV conformance against FASTA input.
patterns <- c(p1 = "ATG", p2 = "TTT")
texts <- c(t1 = "CCCCATGCCCCTTT")
pattern_fa <- write_fasta(patterns, file.path(work, "patterns.fa"))
text_fa <- write_fasta(texts, file.path(work, "texts.fa"))

cli_search <- normalize_cli(read_tsv_lines(run_cli(c(
  "search",
  "--pattern-fasta", pattern_fa,
  "-k", "1",
  "--no-rc",
  "--alphabet", "iupac",
  text_fa
))))
r_search <- search_to_cli(
  sassy_search(
    unname(patterns),
    unname(texts),
    k = 1,
    alphabet = "iupac",
    rc = FALSE,
    strategy = "pairwise",
    match_region = TRUE
  ),
  pattern_ids = names(patterns),
  text_ids = names(texts)
)
expect_identical_df("search TSV", r_search, cli_search)

# Encoded multi-pattern search corresponds to CLI --v2 when reverse-complement
# search is disabled.
cli_v2 <- normalize_cli(read_tsv_lines(run_cli(c(
  "search",
  "--pattern-fasta", pattern_fa,
  "-k", "1",
  "--no-rc",
  "--alphabet", "iupac",
  "--v2",
  text_fa
))))
r_v2 <- search_to_cli(
  sassy_search(
    unname(patterns),
    unname(texts),
    k = 1,
    alphabet = "iupac",
    rc = FALSE,
    strategy = "encoded_patterns",
    match_region = TRUE
  ),
  pattern_ids = names(patterns),
  text_ids = names(texts)
)
expect_identical_df("search --v2", r_v2, cli_v2)

# SAM-compatible text-direction output for reverse-strand matches.
sam_texts <- c(chr1 = "TTTCGTTT")
sam_fa <- write_fasta(sam_texts, file.path(work, "sam.fa"))
cli_sam <- normalize_cli(read_tsv_lines(run_cli(c(
  "search",
  "-p", "ACGA",
  "-k", "0",
  "--alphabet", "dna",
  "--sam",
  sam_fa
))))
r_sam <- search_to_cli(
  sassy_search(
    "ACGA",
    unname(sam_texts),
    k = 0,
    alphabet = "dna",
    match_region = TRUE,
    sam = TRUE
  ),
  pattern_ids = "pattern",
  text_ids = names(sam_texts)
)
expect_identical_df("search --sam", r_sam, cli_sam)

# CRISPR output conformance. The CLI writes progress messages to stdout, so send
# tabular output to a file and compare that file.
guides <- "ACGTNGG"
crispr_texts <- c(chr1 = "TTTACGTAGGTTT", chr2 = "TTTACGTAAATTT")
guide_file <- file.path(work, "guides.txt")
writeLines(guides, guide_file, useBytes = TRUE)
crispr_fa <- write_fasta(crispr_texts, file.path(work, "crispr.fa"))
crispr_out <- file.path(work, "crispr.tsv")
run_cli(c(
  "crispr",
  "--guide", guide_file,
  "-k", "2",
  "--max-n-frac", "0.2",
  "--no-rc",
  "--threads", "1",
  "--output", crispr_out,
  crispr_fa
), stdout = FALSE)
cli_crispr <- normalize_crispr(utils::read.delim(
  crispr_out,
  stringsAsFactors = FALSE,
  check.names = FALSE
))
r_crispr <- normalize_crispr(sassy_crispr(
  guide = guides,
  text = unname(crispr_texts),
  k = 2,
  max_n_frac = 0.2,
  rc = FALSE,
  threads = 1L,
  text_id = names(crispr_texts)
))
expect_identical_df("crispr", r_crispr, cli_crispr)

message("PASS Rsassy/sassy CLI conformance")
