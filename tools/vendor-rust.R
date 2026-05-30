#!/usr/bin/env Rscript

# Vendor Rust crates for CRAN/offline installation.
# Run from the Rsassy package root: Rscript tools/vendor-rust.R
#
# This vendors dependencies from crates.io using src/rust/Cargo.toml.
# Commit the generated src/rust/vendor.tar.xz for offline package builds.

root <- normalizePath(file.path("."), mustWork = TRUE)
src <- file.path(root, "src")
rust <- file.path(src, "rust")
manifest <- file.path(rust, "Cargo.toml")

if (!file.exists(manifest)) {
  stop("Cannot find ", manifest, call. = FALSE)
}

run <- function(cmd, args, wd = root, stdout = "", stderr = "") {
  message("$ ", cmd, " ", paste(args, collapse = " "))
  old <- setwd(wd)
  on.exit(setwd(old), add = TRUE)
  status <- system2(cmd, args, stdout = stdout, stderr = stderr)
  if (!identical(status, 0L)) {
    stop("Command failed: ", cmd, call. = FALSE)
  }
}

replace_manifest_line <- function(lines, key, value, section_name) {
  section_start <- grep(sprintf("^\\[%s\\]\\s*$", section_name), lines)
  if (!length(section_start)) {
    stop("Cannot find [", section_name, "] section in ", manifest, call. = FALSE)
  }
  section_start <- section_start[1L]
  next_section <- grep("^\\[", lines[(section_start + 1L):length(lines)])
  section_end <- if (length(next_section)) section_start + next_section[1L] - 1L else length(lines)
  rel <- section_start:section_end
  idx <- rel[grep(sprintf("^%s\\s*=", key), lines[rel])[1L]]
  if (is.na(idx)) {
    stop("Cannot find ", key, " in [", section_name, "] of ", manifest, call. = FALSE)
  }
  lines[idx] <- value
  lines
}

sync_rsassy_manifest_version <- function() {
  desc <- read.dcf(file.path(root, "DESCRIPTION"))
  package_version <- desc[1L, "Version"]
  lines <- readLines(manifest, warn = FALSE)
  lines <- replace_manifest_line(
    lines,
    "version",
    sprintf('version = "%s"', package_version),
    "package"
  )
  writeLines(lines, manifest)
}

sync_rsassy_manifest_version()

unlink(file.path(src, "vendor"), recursive = TRUE, force = TRUE)
unlink(file.path(rust, "vendor.tar.xz"), force = TRUE)
unlink(file.path(rust, "Cargo.lock"), force = TRUE)

old <- setwd(src)
on.exit(setwd(old), add = TRUE)
config <- system2(
  "cargo",
  c("vendor", "--manifest-path", "rust/Cargo.toml", "--versioned-dirs", "vendor"),
  stdout = TRUE,
  stderr = ""
)
setwd(old)
status <- attr(config, "status")
if (!is.null(status) && status != 0L) {
  stop("cargo vendor failed", call. = FALSE)
}
writeLines(config, file.path(rust, "vendor-config.toml"))

run("Rscript", c("tools/update-authors.R"), wd = root)
run("tar", c("-cJf", "rust/vendor.tar.xz", "vendor"), wd = src)

unlink(file.path(src, "vendor"), recursive = TRUE, force = TRUE)
unlink(file.path(rust, "Cargo.lock"), force = TRUE)

message("Wrote ", file.path(rust, "vendor.tar.xz"))
