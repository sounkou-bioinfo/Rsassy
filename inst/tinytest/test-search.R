expect_silent(sassy_set_backend("auto"))

matches <- sassy_search(list("ACGT"), list("TTACGTAA"), 0, alphabet = "dna", rc = FALSE)
expect_true(is.data.frame(matches))
expect_equal(nrow(matches), 1L)
expect_equal(names(matches), c(
  "pattern_idx", "text_idx", "text_start", "text_end", "pattern_start",
  "pattern_end", "cost", "strand", "cigar"
))
expect_equal(class(matches), c("sassy_matches", "data.frame"))
expect_equal(matches$pattern_idx, 0L)
expect_equal(matches$text_idx, 0L)
expect_equal(matches$text_start, 2)
expect_equal(matches$text_end, 6)
expect_equal(matches$pattern_start, 0)
expect_equal(matches$pattern_end, 4)
expect_equal(matches$cost, 0L)
expect_equal(matches$strand, "+")
expect_equal(matches$cigar, "4=")

region_matches <- sassy_search(list("ATCGATCG"), list("GGGGATCGATCGTTTT"), 1, alphabet = "dna", match_region = TRUE)
expect_true("match_region" %in% names(region_matches))
expect_equal(region_matches$match_region, c("ATCGATCC", "ATCGATCG", "AACGATCG"))
expect_true(!("match_region" %in% names(sassy_search(list("ATCG"), list("ATCG"), 0, alphabet = "dna"))))

rc_region <- sassy_search(list("ACGA"), list("TTTCGTTT"), 0, alphabet = "dna", match_region = TRUE)
rc_row <- rc_region[rc_region$strand == "-", , drop = FALSE]
expect_equal(nrow(rc_row), 1L)
expect_equal(rc_row$match_region, "ACGA")
rc_sam <- sassy_search(list("ACGA"), list("TTTCGTTT"), 0, alphabet = "dna", match_region = TRUE, sam = TRUE)
rc_sam_row <- rc_sam[rc_sam$strand == "-", , drop = FALSE]
expect_equal(rc_sam_row$match_region, "TCGT")
toy_sam <- sassy_as_sam(data.frame(strand = "-", cigar = "2=1X3D", match_region = "AAGT"), alphabet = "dna")
expect_equal(toy_sam$cigar, "3D1X2=")
expect_equal(toy_sam$match_region, "ACTT")

print_output <- capture.output(print(region_matches, color = FALSE))
expect_true(grepl("<sassy_matches> 3 matches", print_output[1], fixed = TRUE))
expect_true(any(grepl("match_region", print_output, fixed = TRUE)))
colored_output <- capture.output(print(region_matches[1, ], color = TRUE))
expect_true(any(grepl("\033", colored_output, fixed = TRUE)))
expect_true(any(grepl("\033[32m", colored_output, fixed = TRUE)))
sub_output <- capture.output(print(region_matches[grepl("X", region_matches$cigar), ][1, ], color = TRUE))
expect_true(any(grepl("\033[38;5;208m", sub_output, fixed = TRUE)))
gap_matches <- sassy_search(list("ACGT"), list("AC"), 2, alphabet = "dna", rc = FALSE, all = TRUE, match_region = TRUE)
gap_output <- capture.output(print(gap_matches[gap_matches$cigar == "2=2I", ][1, ], color = TRUE))
expect_true(any(grepl("\033[31m-", gap_output, fixed = TRUE)))
insert_matches <- sassy_search(list("ACT"), list("ACGT"), 1, alphabet = "dna", rc = FALSE, all = TRUE, match_region = TRUE)
insert_output <- capture.output(print(insert_matches[insert_matches$cigar == "2=1D1=", ][1, ], color = TRUE))
expect_true(any(grepl("\033[34m", insert_output, fixed = TRUE)))

searcher <- sassy_searcher("dna", rc = TRUE)
expect_equal(class(searcher), "sassy_searcher")
rc_matches <- sassy_searcher_search(searcher, list("ACGT"), list("TTACGTAA"), 0)
expect_true(nrow(rc_matches) >= 1L)
expect_true("cigar" %in% names(rc_matches))

bulk_matches <- sassy_search(
  list("ATG", "TTT"),
  list("CCCCATGCCCCTTT"),
  1,
  alphabet = "iupac",
  rc = FALSE,
  strategy = "encoded_patterns"
)
expect_equal(nrow(bulk_matches), 2L)
expect_equal(bulk_matches$pattern_idx, c(0L, 1L))
expect_equal(bulk_matches$text_idx, c(0L, 0L))
expect_equal(bulk_matches$text_start, c(4, 11))
expect_equal(bulk_matches$cigar, c("3=", "3="))

id_matches <- sassy_search(
  list("ATG", "TTT"),
  list("CCCCATG", "TTTATG"),
  0,
  alphabet = "iupac",
  rc = FALSE,
  pattern_id = c("guide_a", "guide_b"),
  text_id = c("record_a", "record_b")
)
expect_true(all(c("pattern_id", "text_id") %in% names(id_matches)))
expect_equal(id_matches$pattern_id, c("guide_a", "guide_b", "guide_a"))
expect_equal(id_matches$text_id, c("record_a", "record_b", "record_b"))
expect_error(sassy_search(list("ATG"), list("CCCCATG", "TTTATG"), 0, text_id = "record_a"))
expect_error(sassy_search("ATG", list("CCCCATG"), 0))
expect_error(sassy_search(charToRaw("ATG"), list("CCCCATG"), 0))

batch_text_matches <- sassy_search(
  list("ATG"),
  list("CCCCATG", "TTTATG"),
  0,
  alphabet = "iupac",
  rc = FALSE,
  strategy = "batch_texts"
)
expect_equal(nrow(batch_text_matches), 2L)
expect_equal(batch_text_matches$pattern_idx, c(0L, 0L))
expect_equal(batch_text_matches$text_idx, c(0L, 1L))
expect_equal(batch_text_matches$text_start, c(4, 3))
expect_equal(batch_text_matches$cigar, c("3=", "3="))

batch_pattern_matches <- sassy_search(
  list("ATG", "TTT"),
  list("CCCCATGCCCCTTT"),
  0,
  alphabet = "iupac",
  rc = FALSE,
  strategy = "batch_patterns"
)
expect_equal(nrow(batch_pattern_matches), 2L)
expect_equal(batch_pattern_matches$pattern_idx, c(0L, 1L))
expect_equal(batch_pattern_matches$text_idx, c(0L, 0L))
expect_equal(batch_pattern_matches$text_start, c(4, 11))
expect_equal(batch_pattern_matches$cigar, c("3=", "3="))

crispr_matches <- sassy_crispr(
  list("ACGTNGG"),
  list("TTTACGTAGGTTT", "TTTACGTAAATTT"),
  2,
  rc = FALSE,
  pattern_id = "guide_a",
  text_id = c("chr1", "chr2")
)
expect_equal(names(crispr_matches), c("pattern_id", "guide", "text_id", "cost", "strand", "start", "end", "match_region", "cigar"))
expect_equal(nrow(crispr_matches), 1L)
expect_equal(crispr_matches$pattern_id, "guide_a")
expect_equal(crispr_matches$guide, "ACGTNGG")
expect_equal(crispr_matches$text_id, "chr1")
expect_equal(crispr_matches$match_region, "ACGTAGG")
crispr_no_ids <- sassy_crispr(list("ACGTNGG"), list("TTTACGTAGGTTT"), 0, rc = FALSE)
expect_equal(names(crispr_no_ids), c("guide", "cost", "strand", "start", "end", "match_region", "cigar"))
crispr_pam_edits <- sassy_crispr(list("ACGTNGG"), list("TTTACGTAAATTT"), 2, rc = FALSE, allow_pam_edits = TRUE)
expect_true(any(crispr_pam_edits$match_region == "ACGTAAA"))
expect_error(sassy_crispr(list("ACGTNGG", "ACGTNGA"), list("TTTACGTAGGTTT"), 0, rc = FALSE))
expect_error(sassy_crispr("ACGTNGG", list("TTTACGTAGGTTT"), 0, rc = FALSE))

many_matches <- sassy_search(
  list("hello", "world"),
  list("hello world", "the world wide web"),
  0,
  alphabet = "ascii",
  strategy = "pairwise",
  threads = 2L
)
expect_equal(nrow(many_matches), 3L)
expect_true(all(c(0L, 1L) %in% many_matches$pattern_idx))
expect_true(all(c(0L, 1L) %in% many_matches$text_idx))

cartesian_texts <- rep("CCCC", 130L)
cartesian_texts[c(2L, 65L, 130L)] <- c("ATGCCC", "CCATGCC", "GGGATG")
cartesian_serial <- sassy_search(list("ATG"), as.list(cartesian_texts), 0, alphabet = "dna", rc = FALSE, strategy = "pairwise", threads = 1L)
cartesian_parallel <- sassy_search(list("ATG"), as.list(cartesian_texts), 0, alphabet = "dna", rc = FALSE, strategy = "pairwise", threads = 4L)
expect_equal(cartesian_parallel, cartesian_serial)
expect_equal(cartesian_parallel$text_idx, c(1L, 64L, 129L))
expect_error(sassy_search(list("ATG"), list("CCCCATG"), 0, alphabet = "dna", rc = FALSE, strategy = "single"))
expect_error(sassy_search(list("ATG"), list("CCCCATG"), 0, alphabet = "dna", rc = FALSE, all = TRUE, strategy = "batch_texts"))
expect_error(sassy_search(list("ATG"), list("CCCCATG"), 0, alphabet = "dna", rc = FALSE, strategy = "not_a_strategy"))
expect_error(.Call(
  "RC_sassy_searcher_search",
  sassy_searcher("dna", rc = FALSE),
  list("ATG"),
  list("CCCCATG"),
  0,
  TRUE,
  1L,
  "batch_texts",
  FALSE,
  NULL,
  NULL,
  PACKAGE = "Rsassy"
))
expect_error(.Call(
  "RC_sassy_searcher_search",
  sassy_searcher("dna", rc = FALSE),
  list("ATG"),
  list("CCCCATG"),
  0,
  FALSE,
  1L,
  "not_a_strategy",
  FALSE,
  NULL,
  NULL,
  PACKAGE = "Rsassy"
))

raw_matches <- sassy_search(list(charToRaw("ACGT")), list(charToRaw("TTACGTAA")), 0, alphabet = "dna", rc = FALSE)
expect_equal(raw_matches, matches)

raw_list_matches <- sassy_search(
  list(charToRaw("ATG"), "TTT"),
  list("CCCCATG", charToRaw("TTTATG")),
  0,
  alphabet = "iupac",
  rc = FALSE
)
expect_equal(nrow(raw_list_matches), 3L)
expect_equal(raw_list_matches$pattern_idx, c(0L, 1L, 0L))
expect_equal(raw_list_matches$text_idx, c(0L, 1L, 1L))
expect_equal(raw_list_matches$text_start, c(4, 0, 3))

features <- sassy_features()
expect_true(is.list(features))
expect_equal(class(features)[1], "sassy_features")
features_output <- capture.output(print(features))
expect_true(any(grepl("<sassy_features>", features_output, fixed = TRUE)))
expect_true(any(grepl("selected backend:", features_output, fixed = TRUE)))
expect_true(all(c("target_arch", "rsassy_selected_backend", "rsassy_installed_backends", "rsassy_supported_backends", "selected_simd_backend", "selected_compiled_avx2", "cpu_avx2", "selected_compiled_wasm_simd128") %in% names(features)))
expect_true(is.character(features$rsassy_installed_backends))
expect_true(is.character(features$rsassy_supported_backends))
expect_true(features$rsassy_selected_backend %in% features$rsassy_installed_backends)
expect_true(features$rsassy_selected_backend %in% features$rsassy_supported_backends)
expect_true(is.character(features$selected_simd_backend))
expect_true(is.logical(features$selected_portable_scalar))
if (identical(features$rsassy_dispatch, "dynamic")) {
  libs_dir <- system.file("libs", package = "Rsassy")
  backends_dir <- system.file("backends", package = "Rsassy")
  expect_true(dir.exists(backends_dir))
  expect_false(any(grepl("^rsassy_backend_", basename(list.files(libs_dir, recursive = TRUE)))))
  expect_true(any(grepl("^rsassy_backend_", basename(list.files(backends_dir, recursive = TRUE)))))
}
expect_error(sassy_set_backend("scalar"))

backend_script <- tempfile(fileext = ".R")
backend_out <- tempfile()
writeLines(c(
  "args <- commandArgs(trailingOnly = TRUE)",
  "library(Rsassy)",
  "sassy_set_backend('scalar')",
  "writeLines(sassy_features()$rsassy_selected_backend, args[[1L]])"
), backend_script)
backend_status <- system2(
  file.path(R.home("bin"), "Rscript"),
  c("--vanilla", backend_script, backend_out)
)
expect_equal(backend_status, 0L)
expect_equal(readLines(backend_out), "scalar")
unlink(c(backend_script, backend_out))

expect_error(sassy_search(list("ACGT"), list("TTACGTAA"), -1, alphabet = "dna"))
expect_error(sassy_search(list("ACGT"), list("TTACGTAA"), 0, alphabet = "dna", alpha = 0.5))
expect_error(sassy_search(list("AT", "TTT"), list("ATTT"), 0, alphabet = "iupac", strategy = "encoded_patterns"))
