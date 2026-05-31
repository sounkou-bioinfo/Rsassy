mod backend;
mod fastx;
mod search_many;

#[cfg(not(target_arch = "wasm32"))]
use rayon::prelude::*;
#[cfg(not(target_arch = "wasm32"))]
use rayon::ThreadPoolBuilder;
use sassy::profiles::{Ascii, Dna, Iupac, Profile};
use sassy::{Match as SassyMatch, Searcher, Strand};
use search_many::{parse_search_strategy, search_many, validate_strategy_for_profile};
use std::cell::RefCell;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::slice;

thread_local! {
    static LAST_ERROR: RefCell<Option<CString>> = const { RefCell::new(None) };
}

enum SearcherType {
    Ascii(Searcher<Ascii>),
    Dna(Searcher<Dna>),
    Iupac(Searcher<Iupac>),
}

pub struct RsassySearcher {
    inner: SearcherType,
}

#[repr(C)]
#[derive(Debug)]
pub struct RsassyMatch {
    pub pattern_idx: usize,
    pub text_idx: usize,
    pub text_start: usize,
    pub text_end: usize,
    pub pattern_start: usize,
    pub pattern_end: usize,
    pub cost: i32,
    /// 0 = forward, 1 = reverse-complement.
    pub strand: u8,
    pub cigar: *mut c_char,
    pub match_region: *mut c_char,
    pub match_region_len: usize,
}

fn set_last_error(message: impl Into<String>) -> c_int {
    let message = message.into().replace('\0', "\\0");
    LAST_ERROR.with(|slot| {
        *slot.borrow_mut() = Some(CString::new(message).expect("interior NULs were replaced"));
    });
    1
}

fn clear_last_error() {
    LAST_ERROR.with(|slot| {
        *slot.borrow_mut() = None;
    });
}

pub(crate) fn cstr_arg<'a>(ptr: *const c_char, arg: &str) -> Result<&'a str, String> {
    if ptr.is_null() {
        return Err(format!("{arg} pointer must not be NULL"));
    }
    unsafe { CStr::from_ptr(ptr) }
        .to_str()
        .map_err(|_| format!("{arg} must be valid UTF-8"))
}

// Never unwind Rust panics or R longjmp errors across this FFI boundary.
// Exported functions return an integer status, store Rust-side error text in
// thread-local storage, and let the C shim raise `Rf_error()` after Rust frames
// have returned. This follows the guidance in:
// https://yutani.rbind.io/post/r-rust-protect-and-unwinding/
fn guard_ffi(fun: impl FnOnce() -> Result<(), String>) -> c_int {
    clear_last_error();
    match catch_unwind(AssertUnwindSafe(fun)) {
        Ok(Ok(())) => 0,
        Ok(Err(err)) => set_last_error(err),
        Err(payload) => {
            let message = if let Some(msg) = payload.downcast_ref::<&str>() {
                (*msg).to_string()
            } else if let Some(msg) = payload.downcast_ref::<String>() {
                msg.clone()
            } else {
                "Rust panic crossing Rsassy FFI boundary".to_string()
            };
            set_last_error(message)
        }
    }
}

unsafe fn bytes_from_raw<'a>(ptr: *const u8, len: usize, arg: &str) -> Result<&'a [u8], String> {
    if len == 0 {
        return Ok(&[]);
    }
    if ptr.is_null() {
        return Err(format!(
            "{arg} pointer must not be NULL when length is non-zero"
        ));
    }
    // SAFETY: The R/C caller provides a pointer to an object that remains live
    // for the duration of the FFI call. We reject NULL for non-empty inputs and
    // only create an immutable slice with the caller-supplied length.
    Ok(unsafe { slice::from_raw_parts(ptr, len) })
}

unsafe fn byte_slices_from_raw_arrays<'a>(
    ptrs: *const *const u8,
    lens: *const usize,
    len: usize,
    arg: &str,
) -> Result<Vec<&'a [u8]>, String> {
    if len == 0 {
        return Ok(Vec::new());
    }
    if ptrs.is_null() || lens.is_null() {
        return Err(format!(
            "{arg} pointer and length arrays must not be NULL when length is non-zero"
        ));
    }

    // SAFETY: The caller promises that both arrays have `len` entries and stay
    // valid for this FFI call. We checked that both pointers are non-NULL when
    // `len > 0`.
    let ptrs = unsafe { slice::from_raw_parts(ptrs, len) };
    // SAFETY: Same as above for the parallel length array.
    let lens = unsafe { slice::from_raw_parts(lens, len) };
    let mut out = Vec::with_capacity(len);
    for i in 0..len {
        // SAFETY: Each element pointer/length pair is validated by
        // `bytes_from_raw` before it constructs the slice.
        out.push(unsafe { bytes_from_raw(ptrs[i], lens[i], &format!("{arg}[{}]", i + 1))? });
    }
    Ok(out)
}

fn parse_alphabet_and_alpha(
    alphabet: *const c_char,
    alpha: f32,
) -> Result<(String, Option<f32>), String> {
    if alphabet.is_null() {
        return Err("alphabet pointer must not be NULL".to_string());
    }
    // SAFETY: `alphabet` was checked for NULL above and is expected to be a
    // NUL-terminated C string owned by the C/R caller for this call.
    let alphabet = unsafe { CStr::from_ptr(alphabet) }
        .to_str()
        .map_err(|_| "alphabet must be valid UTF-8".to_string())?
        .to_ascii_lowercase();

    let alpha = if alpha.is_nan() {
        None
    } else {
        if !(0.0..=1.0).contains(&alpha) {
            return Err("alpha must be NaN or in [0, 1]".to_string());
        }
        if alphabet != "iupac" {
            return Err("alpha overhang cost is only supported for alphabet = 'iupac'".to_string());
        }
        Some(alpha)
    };

    Ok((alphabet, alpha))
}

fn boxed_bytes_into_raw(bytes: Vec<u8>) -> (*mut c_char, usize) {
    let len = bytes.len();
    if len == 0 {
        return (ptr::null_mut(), 0);
    }
    let mut bytes = bytes.into_boxed_slice();
    let ptr = bytes.as_mut_ptr().cast::<c_char>();
    std::mem::forget(bytes);
    (ptr, len)
}

fn match_region_for<P: Profile>(m: &SassyMatch, texts: &[&[u8]]) -> Result<Vec<u8>, String> {
    let text = texts
        .get(m.text_idx)
        .ok_or_else(|| format!("match text_idx {} is outside input text bounds", m.text_idx))?;
    if m.text_start > m.text_end || m.text_end > text.len() {
        return Err("match coordinates are outside text bounds".to_string());
    }
    let slice = &text[m.text_start..m.text_end];
    Ok(if m.strand == Strand::Rc {
        P::reverse_complement(slice)
    } else {
        slice.to_vec()
    })
}

fn rsassy_match_from<P: Profile>(
    value: SassyMatch,
    texts: &[&[u8]],
    include_match_region: bool,
) -> Result<RsassyMatch, String> {
    let region_bytes = if include_match_region {
        Some(match_region_for::<P>(&value, texts)?)
    } else {
        None
    };
    let cigar = CString::new(value.cigar.to_string())
        .expect("CIGAR strings cannot contain NUL bytes")
        .into_raw();
    let (match_region, match_region_len) = match region_bytes {
        Some(bytes) => boxed_bytes_into_raw(bytes),
        None => (ptr::null_mut(), 0),
    };
    Ok(RsassyMatch {
        pattern_idx: value.pattern_idx,
        text_idx: value.text_idx,
        text_start: value.text_start,
        text_end: value.text_end,
        pattern_start: value.pattern_start,
        pattern_end: value.pattern_end,
        cost: value.cost,
        strand: match value.strand {
            Strand::Fwd => 0,
            Strand::Rc => 1,
        },
        cigar,
        match_region,
        match_region_len,
    })
}

fn write_match_array<P: Profile>(
    matches: Vec<SassyMatch>,
    texts: &[&[u8]],
    include_match_region: bool,
    out_matches: *mut *mut RsassyMatch,
    out_len: *mut usize,
) -> Result<(), String> {
    if out_matches.is_null() || out_len.is_null() {
        return Err("output match pointers must not be NULL".to_string());
    }

    let mut out = Vec::with_capacity(matches.len());
    for m in matches {
        out.push(rsassy_match_from::<P>(m, texts, include_match_region)?);
    }
    if out.is_empty() {
        // SAFETY: `write_match_array` validated both output pointers above.
        // Writing a NULL pointer plus zero length is the FFI representation of
        // an empty result.
        unsafe {
            *out_matches = ptr::null_mut();
            *out_len = 0;
        }
        return Ok(());
    }

    let len = out.len();
    let mut matches = out.into_boxed_slice();
    let ptr = matches.as_mut_ptr();
    std::mem::forget(matches);

    // SAFETY: `write_match_array` validated both output pointers above. The
    // boxed slice allocation is intentionally leaked with `forget`; ownership
    // is transferred to the C/R caller, which must call `rsassy_matches_free`
    // with the same pointer and length.
    unsafe {
        *out_matches = ptr;
        *out_len = len;
    }
    Ok(())
}

fn iupac_match_slices(query: &[u8], target: &[u8]) -> bool {
    query.len() == target.len()
        && query
            .iter()
            .zip(target.iter())
            .all(|(q, t)| Iupac::is_match(*q, *t))
}

fn n_fraction_ok(max_n_frac: f32, slice: &[u8]) -> bool {
    if slice.is_empty() {
        return true;
    }
    let n_count = slice.iter().filter(|c| (**c & 0xDF) == b'N').count() as f32;
    n_count / slice.len() as f32 <= max_n_frac
}

fn crispr_common_pam(guides: &[&[u8]], pam_length: usize) -> Result<Vec<u8>, String> {
    if guides.is_empty() {
        return Err("guide must contain at least one sequence".to_string());
    }
    if pam_length == 0 {
        return Err("pam_length must be >= 1".to_string());
    }
    let first = guides[0];
    if first.len() < pam_length {
        return Err("all guide sequences must be at least pam_length bytes long".to_string());
    }
    let pam = &first[first.len() - pam_length..];
    for guide in guides {
        if guide.len() < pam_length {
            return Err("all guide sequences must be at least pam_length bytes long".to_string());
        }
        if &guide[guide.len() - pam_length..] != pam {
            return Err("all guide sequences must have the same PAM suffix".to_string());
        }
    }
    Ok(pam.to_vec())
}

#[allow(clippy::too_many_arguments)]
fn crispr_search_pair(
    searcher: &mut Searcher<Iupac>,
    guide: &[u8],
    text: &[u8],
    k: usize,
    pam_length: usize,
    pam: &[u8],
    pam_complement: &[u8],
    allow_pam_edits: bool,
    max_n_frac: f32,
    pattern_idx: usize,
    text_idx: usize,
) -> Vec<SassyMatch> {
    let mut matches = if allow_pam_edits {
        searcher.search_all(guide, text, k)
    } else {
        searcher.search_with_fn(guide, text, k, true, |_, text_up_to_end, strand| {
            if text_up_to_end.len() < pam_length {
                return false;
            }
            let pam_slice = &text_up_to_end[text_up_to_end.len() - pam_length..];
            if strand == Strand::Fwd {
                iupac_match_slices(pam_slice, pam)
            } else {
                iupac_match_slices(pam_slice, pam_complement)
            }
        })
    };

    matches.retain(|m| {
        m.text_start <= m.text_end
            && m.text_end <= text.len()
            && n_fraction_ok(max_n_frac, &text[m.text_start..m.text_end])
    });
    for m in &mut matches {
        m.pattern_idx = pattern_idx;
        m.text_idx = text_idx;
    }
    matches
}

#[allow(clippy::too_many_arguments)]
fn crispr_search_many_serial(
    guides: &[&[u8]],
    texts: &[&[u8]],
    k: usize,
    pam_length: usize,
    pam: &[u8],
    pam_complement: &[u8],
    allow_pam_edits: bool,
    max_n_frac: f32,
    rc: bool,
) -> Vec<SassyMatch> {
    let mut searcher = Searcher::<Iupac>::new(rc, None);
    let mut out = Vec::new();
    for (text_idx, text) in texts.iter().enumerate() {
        for (pattern_idx, guide) in guides.iter().enumerate() {
            out.extend(crispr_search_pair(
                &mut searcher,
                guide,
                text,
                k,
                pam_length,
                pam,
                pam_complement,
                allow_pam_edits,
                max_n_frac,
                pattern_idx,
                text_idx,
            ));
        }
    }
    out
}

#[cfg(not(target_arch = "wasm32"))]
#[allow(clippy::too_many_arguments)]
fn crispr_search_many_parallel(
    guides: &[&[u8]],
    texts: &[&[u8]],
    k: usize,
    pam_length: usize,
    pam: &[u8],
    pam_complement: &[u8],
    allow_pam_edits: bool,
    max_n_frac: f32,
    rc: bool,
) -> Vec<SassyMatch> {
    const TASKS_PER_CHUNK: usize = 64;
    let n_guides = guides.len();
    let n_pairs = n_guides * texts.len();
    let mut chunks: Vec<(usize, Vec<SassyMatch>)> = (0..n_pairs)
        .into_par_iter()
        .chunks(TASKS_PER_CHUNK)
        .enumerate()
        .map(|(chunk_idx, pairs)| {
            let mut searcher = Searcher::<Iupac>::new(rc, None);
            let mut out = Vec::new();
            for pair_idx in pairs {
                let text_idx = pair_idx / n_guides;
                let pattern_idx = pair_idx % n_guides;
                out.extend(crispr_search_pair(
                    &mut searcher,
                    guides[pattern_idx],
                    texts[text_idx],
                    k,
                    pam_length,
                    pam,
                    pam_complement,
                    allow_pam_edits,
                    max_n_frac,
                    pattern_idx,
                    text_idx,
                ));
            }
            (chunk_idx, out)
        })
        .collect();
    chunks.sort_by_key(|(chunk_idx, _)| *chunk_idx);
    chunks
        .into_iter()
        .flat_map(|(_, matches)| matches)
        .collect()
}

#[allow(clippy::too_many_arguments)]
fn crispr_search_many(
    guides: &[&[u8]],
    texts: &[&[u8]],
    k: usize,
    pam_length: usize,
    pam: &[u8],
    pam_complement: &[u8],
    allow_pam_edits: bool,
    max_n_frac: f32,
    rc: bool,
    threads: usize,
) -> Vec<SassyMatch> {
    if guides.is_empty() || texts.is_empty() {
        return Vec::new();
    }
    #[cfg(not(target_arch = "wasm32"))]
    {
        if threads > 1 {
            if let Ok(pool) = ThreadPoolBuilder::new().num_threads(threads).build() {
                return pool.install(|| {
                    crispr_search_many_parallel(
                        guides,
                        texts,
                        k,
                        pam_length,
                        pam,
                        pam_complement,
                        allow_pam_edits,
                        max_n_frac,
                        rc,
                    )
                });
            }
        }
    }
    crispr_search_many_serial(
        guides,
        texts,
        k,
        pam_length,
        pam,
        pam_complement,
        allow_pam_edits,
        max_n_frac,
        rc,
    )
}

#[no_mangle]
pub extern "C" fn rsassy_searcher_new(
    alphabet: *const c_char,
    rc: bool,
    alpha: f32,
    out: *mut *mut RsassySearcher,
) -> c_int {
    guard_ffi(|| {
        if out.is_null() {
            return Err("out searcher pointer must not be NULL".to_string());
        }

        let (alphabet, alpha) = parse_alphabet_and_alpha(alphabet, alpha)?;
        let inner = match alphabet.as_str() {
            // The ASCII profile has no reverse-complement operation. Match the
            // Python binding and always search ASCII on the forward strand.
            "ascii" => SearcherType::Ascii(Searcher::<Ascii>::new(false, None)),
            "dna" => SearcherType::Dna(Searcher::<Dna>::new(rc, None)),
            "iupac" => SearcherType::Iupac(Searcher::<Iupac>::new(rc, alpha)),
            _ => return Err(format!("unsupported alphabet: {alphabet}")),
        };

        let boxed = Box::new(RsassySearcher { inner });
        // SAFETY: `out` was checked for NULL above. The boxed searcher is
        // transferred to the C/R caller and later reclaimed by
        // `rsassy_searcher_free`.
        unsafe {
            *out = Box::into_raw(boxed);
        }
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn rsassy_searcher_free(searcher: *mut RsassySearcher) {
    if !searcher.is_null() {
        // SAFETY: The pointer must have been returned by `rsassy_searcher_new`.
        // This function is the unique destructor registered on the R external
        // pointer and tolerates NULL by doing nothing.
        unsafe {
            drop(Box::from_raw(searcher));
        }
    }
}

#[no_mangle]
pub extern "C" fn rsassy_searcher_search(
    searcher: *mut RsassySearcher,
    pattern: *const u8,
    pattern_len: usize,
    text: *const u8,
    text_len: usize,
    k: usize,
    all_matches: bool,
    include_match_region: bool,
    out_matches: *mut *mut RsassyMatch,
    out_len: *mut usize,
) -> c_int {
    guard_ffi(|| {
        if searcher.is_null() {
            return Err("searcher pointer must not be NULL".to_string());
        }

        // SAFETY: `bytes_from_raw` checks NULL/length invariants before making
        // immutable slices that are used only during this call.
        let pattern = unsafe { bytes_from_raw(pattern, pattern_len, "pattern")? };
        // SAFETY: Same as above for the text input.
        let text = unsafe { bytes_from_raw(text, text_len, "text")? };
        // SAFETY: `searcher` was checked for NULL above and points to an
        // `RsassySearcher` created by `rsassy_searcher_new`. R calls this API
        // synchronously, so we borrow it mutably only for the duration here.
        let searcher = unsafe { &mut *searcher };

        match &mut searcher.inner {
            SearcherType::Ascii(searcher) => {
                let matches = if all_matches {
                    searcher.search_all(pattern, text, k)
                } else {
                    searcher.search(pattern, text, k)
                };
                write_match_array::<Ascii>(
                    matches,
                    &[text],
                    include_match_region,
                    out_matches,
                    out_len,
                )
            }
            SearcherType::Dna(searcher) => {
                let matches = if all_matches {
                    searcher.search_all(pattern, text, k)
                } else {
                    searcher.search(pattern, text, k)
                };
                write_match_array::<Dna>(
                    matches,
                    &[text],
                    include_match_region,
                    out_matches,
                    out_len,
                )
            }
            SearcherType::Iupac(searcher) => {
                let matches = if all_matches {
                    searcher.search_all(pattern, text, k)
                } else {
                    searcher.search(pattern, text, k)
                };
                write_match_array::<Iupac>(
                    matches,
                    &[text],
                    include_match_region,
                    out_matches,
                    out_len,
                )
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rsassy_searcher_search_many(
    searcher: *mut RsassySearcher,
    patterns: *const *const u8,
    pattern_lens: *const usize,
    n_patterns: usize,
    texts: *const *const u8,
    text_lens: *const usize,
    n_texts: usize,
    k: usize,
    all_matches: bool,
    threads: usize,
    strategy: *const c_char,
    include_match_region: bool,
    out_matches: *mut *mut RsassyMatch,
    out_len: *mut usize,
) -> c_int {
    guard_ffi(|| {
        if searcher.is_null() {
            return Err("searcher pointer must not be NULL".to_string());
        }
        let strategy = parse_search_strategy(strategy)?;
        // SAFETY: `byte_slices_from_raw_arrays` validates the top-level arrays
        // and each pointer/length pair before constructing borrowed slices.
        let patterns =
            unsafe { byte_slices_from_raw_arrays(patterns, pattern_lens, n_patterns, "pattern")? };
        // SAFETY: Same as above for the text vector.
        let texts = unsafe { byte_slices_from_raw_arrays(texts, text_lens, n_texts, "text")? };
        // SAFETY: `searcher` was checked for NULL above and points to an
        // `RsassySearcher` created by `rsassy_searcher_new`; the mutable borrow
        // is scoped to this synchronous FFI call.
        let searcher = unsafe { &mut *searcher };
        let threads = threads.max(1);

        match &mut searcher.inner {
            SearcherType::Ascii(searcher) => {
                validate_strategy_for_profile("ascii", strategy)?;
                let matches = search_many(
                    searcher,
                    &patterns,
                    &texts,
                    k,
                    all_matches,
                    threads,
                    strategy,
                )?;
                write_match_array::<Ascii>(
                    matches,
                    &texts,
                    include_match_region,
                    out_matches,
                    out_len,
                )
            }
            SearcherType::Dna(searcher) => {
                validate_strategy_for_profile("dna", strategy)?;
                let matches = search_many(
                    searcher,
                    &patterns,
                    &texts,
                    k,
                    all_matches,
                    threads,
                    strategy,
                )?;
                write_match_array::<Dna>(
                    matches,
                    &texts,
                    include_match_region,
                    out_matches,
                    out_len,
                )
            }
            SearcherType::Iupac(searcher) => {
                validate_strategy_for_profile("iupac", strategy)?;
                let matches = search_many(
                    searcher,
                    &patterns,
                    &texts,
                    k,
                    all_matches,
                    threads,
                    strategy,
                )?;
                write_match_array::<Iupac>(
                    matches,
                    &texts,
                    include_match_region,
                    out_matches,
                    out_len,
                )
            }
        }
    })
}

#[no_mangle]
pub extern "C" fn rsassy_crispr_search_many(
    guides: *const *const u8,
    guide_lens: *const usize,
    n_guides: usize,
    texts: *const *const u8,
    text_lens: *const usize,
    n_texts: usize,
    k: usize,
    pam_length: usize,
    allow_pam_edits: bool,
    max_n_frac: f32,
    rc: bool,
    threads: usize,
    out_matches: *mut *mut RsassyMatch,
    out_len: *mut usize,
) -> c_int {
    guard_ffi(|| {
        let guides = unsafe { byte_slices_from_raw_arrays(guides, guide_lens, n_guides, "guide")? };
        let texts = unsafe { byte_slices_from_raw_arrays(texts, text_lens, n_texts, "text")? };
        if !max_n_frac.is_finite() || !(0.0..=1.0).contains(&max_n_frac) {
            return Err("max_n_frac must be a number in [0, 1]".to_string());
        }
        let pam = crispr_common_pam(&guides, pam_length)?;
        let pam_complement = Iupac::complement(&pam);
        let matches = crispr_search_many(
            &guides,
            &texts,
            k,
            pam_length,
            &pam,
            &pam_complement,
            allow_pam_edits,
            max_n_frac,
            rc,
            threads.max(1),
        );
        write_match_array::<Iupac>(matches, &texts, true, out_matches, out_len)
    })
}

#[no_mangle]
pub extern "C" fn rsassy_matches_free(matches: *mut RsassyMatch, len: usize) {
    if !matches.is_null() {
        // SAFETY: `matches` must be the pointer returned by this library with
        // the same `len`. `write_match_array` transfers a boxed slice, so
        // reconstructing `Box<[RsassyMatch]>` with the same length deallocates
        // the allocation exactly. Each CIGAR pointer came from
        // `CString::into_raw`; each match-region pointer came from a boxed byte
        // slice. Both are reclaimed once here before dropping the boxed match
        // slice.
        unsafe {
            let slice = ptr::slice_from_raw_parts_mut(matches, len);
            for m in &mut *slice {
                if !m.cigar.is_null() {
                    drop(CString::from_raw(m.cigar));
                    m.cigar = ptr::null_mut();
                }
                if !m.match_region.is_null() {
                    let region = ptr::slice_from_raw_parts_mut(
                        m.match_region.cast::<u8>(),
                        m.match_region_len,
                    );
                    drop(Box::from_raw(region));
                    m.match_region = ptr::null_mut();
                    m.match_region_len = 0;
                }
            }
            drop(Box::from_raw(slice));
        }
    }
}

#[no_mangle]
pub extern "C" fn rsassy_last_error_message() -> *const c_char {
    static EMPTY: &[u8] = b"\0";
    LAST_ERROR.with(|slot| match slot.borrow().as_ref() {
        Some(message) => message.as_ptr(),
        None => EMPTY.as_ptr().cast(),
    })
}
