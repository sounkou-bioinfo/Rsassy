expect_silent(sassy_set_backend("auto"))

matches <- sassy_search("ACGT", "TTACGTAA", 0, alphabet = "dna", rc = FALSE)
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

region_matches <- sassy_search("ATCGATCG", "GGGGATCGATCGTTTT", 1, alphabet = "dna", match_region = TRUE)
expect_true("match_region" %in% names(region_matches))
expect_equal(region_matches$match_region, c("ATCGATCG", "AACGATCG", "ATCGATCC"))
expect_true(!("match_region" %in% names(sassy_search("ATCG", "ATCG", 0, alphabet = "dna"))))

print_output <- capture.output(print(region_matches, color = FALSE))
expect_true(grepl("<sassy_matches> 3 matches", print_output[1], fixed = TRUE))
expect_true(any(grepl("match_region", print_output, fixed = TRUE)))
colored_output <- capture.output(print(region_matches[1, ], color = TRUE))
expect_true(any(grepl("\033", colored_output, fixed = TRUE)))
expect_true(any(grepl("\033[32m", colored_output, fixed = TRUE)))
sub_output <- capture.output(print(region_matches[2, ], color = TRUE))
expect_true(any(grepl("\033[38;5;208m", sub_output, fixed = TRUE)))
gap_matches <- sassy_search("ACGT", "AC", 2, alphabet = "dna", rc = FALSE, all = TRUE, match_region = TRUE)
gap_output <- capture.output(print(gap_matches[gap_matches$cigar == "2=2I", ][1, ], color = TRUE))
expect_true(any(grepl("\033[31m-", gap_output, fixed = TRUE)))
insert_matches <- sassy_search("ACT", "ACGT", 1, alphabet = "dna", rc = FALSE, all = TRUE, match_region = TRUE)
insert_output <- capture.output(print(insert_matches[insert_matches$cigar == "2=1D1=", ][1, ], color = TRUE))
expect_true(any(grepl("\033[34m", insert_output, fixed = TRUE)))

searcher <- sassy_searcher("dna", rc = TRUE)
expect_equal(class(searcher), "sassy_searcher")
rc_matches <- sassy_searcher_search(searcher, "ACGT", "TTACGTAA", 0)
expect_true(nrow(rc_matches) >= 1L)
expect_true("cigar" %in% names(rc_matches))

bulk_matches <- sassy_search(
  c("ATG", "TTT"),
  "CCCCATGCCCCTTT",
  1,
  alphabet = "iupac",
  rc = FALSE,
  mode = "encoded_patterns"
)
expect_equal(nrow(bulk_matches), 2L)
expect_equal(bulk_matches$pattern_idx, c(0L, 1L))
expect_equal(bulk_matches$text_idx, c(0L, 0L))
expect_equal(bulk_matches$text_start, c(4, 11))
expect_equal(bulk_matches$cigar, c("3=", "3="))

batch_text_matches <- sassy_search(
  "ATG",
  c("CCCCATG", "TTTATG"),
  0,
  alphabet = "iupac",
  rc = FALSE,
  mode = "batch_texts"
)
expect_equal(nrow(batch_text_matches), 2L)
expect_equal(batch_text_matches$pattern_idx, c(0L, 0L))
expect_equal(batch_text_matches$text_idx, c(0L, 1L))
expect_equal(batch_text_matches$text_start, c(4, 3))
expect_equal(batch_text_matches$cigar, c("3=", "3="))

batch_pattern_matches <- sassy_search(
  c("ATG", "TTT"),
  "CCCCATGCCCCTTT",
  0,
  alphabet = "iupac",
  rc = FALSE,
  mode = "batch_patterns"
)
expect_equal(nrow(batch_pattern_matches), 2L)
expect_equal(batch_pattern_matches$pattern_idx, c(0L, 1L))
expect_equal(batch_pattern_matches$text_idx, c(0L, 0L))
expect_equal(batch_pattern_matches$text_start, c(4, 11))
expect_equal(batch_pattern_matches$cigar, c("3=", "3="))

many_matches <- sassy_search(
  c("hello", "world"),
  c("hello world", "the world wide web"),
  0,
  alphabet = "ascii",
  mode = "single",
  threads = 2L
)
expect_equal(nrow(many_matches), 3L)
expect_true(all(c(0L, 1L) %in% many_matches$pattern_idx))
expect_true(all(c(0L, 1L) %in% many_matches$text_idx))

raw_matches <- sassy_search(charToRaw("ACGT"), charToRaw("TTACGTAA"), 0, alphabet = "dna", rc = FALSE)
expect_equal(raw_matches, matches)

raw_list_matches <- sassy_search(
  list(charToRaw("ATG"), charToRaw("TTT")),
  list(charToRaw("CCCCATG"), charToRaw("TTTATG")),
  0,
  alphabet = "iupac",
  rc = FALSE
)
expect_equal(nrow(raw_list_matches), 3L)
expect_equal(raw_list_matches$pattern_idx, c(0L, 0L, 1L))
expect_equal(raw_list_matches$text_idx, c(0L, 1L, 1L))
expect_equal(raw_list_matches$text_start, c(4, 3, 0))

con <- rawConnection(charToRaw("TTACGTAA"), "rb")
connection_matches <- sassy_search_connection("ACGT", con, 0, alphabet = "dna", rc = FALSE, chunk_size = 3)
close(con)
expect_equal(connection_matches, matches)

con <- rawConnection(charToRaw("TTACGTAA"), "rb")
connection_region_matches <- sassy_search_connection("ACGT", con, 0, alphabet = "dna", rc = FALSE, chunk_size = 3, match_region = TRUE)
close(con)
expect_true("match_region" %in% names(connection_region_matches))
expect_equal(connection_region_matches$match_region, "ACGT")

boundary_text <- paste0("AAAA", "AC", "GT", "AAAA")
con <- rawConnection(charToRaw(boundary_text), "rb")
boundary_matches <- sassy_search_connection("ACGT", con, 0, alphabet = "dna", rc = FALSE, chunk_size = 6)
close(con)
expect_equal(nrow(boundary_matches), 1L)
expect_equal(boundary_matches$text_start, 4)
expect_equal(boundary_matches$text_end, 8)
expect_equal(boundary_matches$cigar, "4=")

con <- rawConnection(charToRaw("CCCCATGCCCCTTT"), "rb")
connection_bulk_matches <- sassy_search_connection(
  c("ATG", "TTT"),
  con,
  1,
  alphabet = "iupac",
  rc = FALSE,
  mode = "encoded_patterns",
  chunk_size = 8
)
close(con)
expect_equal(nrow(connection_bulk_matches), 2L)
expect_equal(connection_bulk_matches$pattern_idx, c(0L, 1L))
expect_equal(connection_bulk_matches$text_idx, c(0L, 0L))
expect_equal(connection_bulk_matches$text_start, c(4, 11))

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

expect_error(sassy_search("ACGT", "TTACGTAA", -1, alphabet = "dna"))
expect_error(sassy_search("ACGT", "TTACGTAA", 0, alphabet = "dna", alpha = 0.5))
expect_error(sassy_search(c("AT", "TTT"), "ATTT", 0, alphabet = "iupac", mode = "encoded_patterns"))
