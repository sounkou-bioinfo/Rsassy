PKGNAME := $(shell sed -n 's/Package: *\([^ ]*\)/\1/p' DESCRIPTION)
PKGVERS := $(shell sed -n 's/Version: *\([^ ]*\)/\1/p' DESCRIPTION)
CARGO_JOBS ?= 2
LOCAL_CARGO_JOBS ?= 10
CHECK_ARGS ?= --no-manual --no-vignettes

# CRAN-safe default is two Cargo jobs. Use e.g. `make CARGO_JOBS=10 check`
# or the local fast targets below when building on a developer machine.
export CARGO_BUILD_JOBS = $(CARGO_JOBS)

all: check

rd:
	Rscript -e 'roxygen2::roxygenize(load_code = "source")'

readme:
	Rscript -e 'rmarkdown::render("README.Rmd", output_format = "github_document", quiet = FALSE)'
	@rm -f README.html

build:
	R CMD build .

cran-build: vendor-rust build

check: build
	R CMD check $(CHECK_ARGS) $(PKGNAME)_$(PKGVERS).tar.gz

check-fast: CARGO_JOBS := $(LOCAL_CARGO_JOBS)
check-fast: check

check-cran: cran-build
	R CMD check --as-cran --no-manual $(PKGNAME)_$(PKGVERS).tar.gz

install_deps:
	Rscript -e 'if (!requireNamespace("tinytest", quietly = TRUE)) install.packages("tinytest")'

install:
	R CMD INSTALL --preclean .

test: install
	Rscript -e 'tinytest::test_package("$(PKGNAME)", testdir = "inst/tinytest", ncpu = 1L)'

test2: install
	Rscript -e 'tinytest::test_package("$(PKGNAME)", testdir = "inst/tinytest", ncpu = 2L)'

test-fast: CARGO_JOBS := $(LOCAL_CARGO_JOBS)
test-fast: test

authors:
	Rscript tools/update-authors.R

vendor-rust:
	Rscript tools/vendor-rust.R

clean:
	@rm -rf $(PKGNAME)_$(PKGVERS).tar.gz $(PKGNAME)_$(PKGVERS).tgz $(PKGNAME).Rcheck README.html \
		src/.cargo src/Makevars src/Makevars.win src/vendor src/rust/vendor src/rust/target src/rust/Cargo.lock \
		src/rsassy-backends src/*.o src/*.so src/*.dll src/*.dylib

.PHONY: all rd readme build cran-build check check-fast check-cran install_deps install test test2 test-fast authors vendor-rust clean
