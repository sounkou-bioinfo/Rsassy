#[cfg(not(target_arch = "wasm32"))]
use rayon::{ThreadPoolBuilder, prelude::*};
use sassy::profiles::Profile;
use sassy::{Match as SassyMatch, Searcher};
use std::os::raw::c_char;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum RsassySearchMode {
    Single,
    BatchPatterns,
    BatchTexts,
    EncodedPatterns,
}

impl RsassySearchMode {
    fn as_str(self) -> &'static str {
        match self {
            RsassySearchMode::Single => "single",
            RsassySearchMode::BatchPatterns => "batch_patterns",
            RsassySearchMode::BatchTexts => "batch_texts",
            RsassySearchMode::EncodedPatterns => "encoded_patterns",
        }
    }
}

pub(crate) fn parse_search_mode(mode: *const c_char) -> Result<RsassySearchMode, String> {
    let mode = crate::cstr_arg(mode, "mode")?;
    match mode {
        "single" => Ok(RsassySearchMode::Single),
        "batch_patterns" => Ok(RsassySearchMode::BatchPatterns),
        "batch_texts" => Ok(RsassySearchMode::BatchTexts),
        "encoded_patterns" | "v2" => Ok(RsassySearchMode::EncodedPatterns),
        _ => Err(format!(
            "unsupported search mode: {mode}; expected 'single', 'batch_patterns', 'batch_texts', or 'encoded_patterns'"
        )),
    }
}

fn serial_search_many<P: Profile>(
    searcher: &mut Searcher<P>,
    patterns: &[&[u8]],
    texts: &[&[u8]],
    k: usize,
    all_matches: bool,
    mode: RsassySearchMode,
) -> Vec<SassyMatch> {
    if patterns.is_empty() || texts.is_empty() {
        return Vec::new();
    }

    if all_matches || mode == RsassySearchMode::Single {
        let mut out = Vec::new();
        for (pattern_idx, pattern) in patterns.iter().enumerate() {
            for (text_idx, text) in texts.iter().enumerate() {
                let mut matches = if all_matches {
                    searcher.search_all(pattern, *text, k)
                } else {
                    searcher.search(pattern, *text, k)
                };
                for m in &mut matches {
                    m.pattern_idx = pattern_idx;
                    m.text_idx = text_idx;
                }
                out.extend(matches);
            }
        }
        return out;
    }

    match mode {
        RsassySearchMode::Single => unreachable!(),
        RsassySearchMode::BatchPatterns => {
            let mut out = Vec::new();
            for (text_idx, text) in texts.iter().enumerate() {
                let mut matches = searcher.search_patterns(patterns, *text, k);
                for m in &mut matches {
                    m.text_idx = text_idx;
                }
                out.extend(matches);
            }
            out
        }
        RsassySearchMode::BatchTexts => {
            let mut out = Vec::new();
            for (pattern_idx, pattern) in patterns.iter().enumerate() {
                let mut matches = searcher.search_texts(pattern, texts, k);
                for m in &mut matches {
                    m.pattern_idx = pattern_idx;
                }
                out.extend(matches);
            }
            out
        }
        RsassySearchMode::EncodedPatterns => {
            search_encoded_patterns_many(searcher, patterns, texts, k, all_matches)
        }
    }
}

fn search_encoded_patterns_many<P: Profile>(
    searcher: &mut Searcher<P>,
    patterns: &[&[u8]],
    texts: &[&[u8]],
    k: usize,
    all_matches: bool,
) -> Vec<SassyMatch> {
    let pattern_vecs: Vec<Vec<u8>> = patterns.iter().map(|pattern| pattern.to_vec()).collect();
    let encoded = searcher.encode_patterns(&pattern_vecs);
    let mut out = Vec::new();

    for (text_idx, text) in texts.iter().enumerate() {
        let matches = if all_matches {
            searcher.search_all_encoded_patterns(&encoded, text, k)
        } else {
            searcher.search_encoded_patterns(&encoded, text, k)
        };
        out.extend(matches.iter().cloned().map(|mut m| {
            m.text_idx = text_idx;
            m
        }));
    }

    out
}

#[cfg(not(target_arch = "wasm32"))]
fn parallel_search_many<P: Profile>(
    searcher: &Searcher<P>,
    patterns: &[&[u8]],
    texts: &[&[u8]],
    k: usize,
    all_matches: bool,
    mode: RsassySearchMode,
) -> Vec<SassyMatch>
where
    Searcher<P>: Clone + Send,
{
    let mode = if all_matches {
        RsassySearchMode::Single
    } else {
        mode
    };

    match mode {
        RsassySearchMode::Single => (0..patterns.len())
            .into_par_iter()
            .flat_map_iter(|pattern_idx| {
                let mut local = searcher.clone();
                let mut out = Vec::new();
                for (text_idx, text) in texts.iter().enumerate() {
                    let mut matches = if all_matches {
                        local.search_all(patterns[pattern_idx], *text, k)
                    } else {
                        local.search(patterns[pattern_idx], *text, k)
                    };
                    for m in &mut matches {
                        m.pattern_idx = pattern_idx;
                        m.text_idx = text_idx;
                    }
                    out.extend(matches);
                }
                out
            })
            .collect(),
        RsassySearchMode::BatchPatterns => texts
            .par_iter()
            .enumerate()
            .flat_map_iter(|(text_idx, text)| {
                let mut local = searcher.clone();
                let mut matches = local.search_patterns(patterns, *text, k);
                for m in &mut matches {
                    m.text_idx = text_idx;
                }
                matches
            })
            .collect(),
        RsassySearchMode::BatchTexts => patterns
            .par_iter()
            .enumerate()
            .flat_map_iter(|(pattern_idx, pattern)| {
                let mut local = searcher.clone();
                let mut matches = local.search_texts(pattern, texts, k);
                for m in &mut matches {
                    m.pattern_idx = pattern_idx;
                }
                matches
            })
            .collect(),
        RsassySearchMode::EncodedPatterns => {
            let mut local = searcher.clone();
            search_encoded_patterns_many(&mut local, patterns, texts, k, all_matches)
        }
    }
}

pub(crate) fn validate_mode_for_profile(
    alphabet: &str,
    mode: RsassySearchMode,
) -> Result<(), String> {
    if matches!(
        mode,
        RsassySearchMode::BatchPatterns | RsassySearchMode::EncodedPatterns
    ) && alphabet != "iupac"
    {
        return Err(format!(
            "mode '{}' uses Sassy multi-pattern encoding and requires alphabet = 'iupac'",
            mode.as_str()
        ));
    }
    Ok(())
}

pub(crate) fn search_many<P: Profile>(
    searcher: &mut Searcher<P>,
    patterns: &[&[u8]],
    texts: &[&[u8]],
    k: usize,
    all_matches: bool,
    threads: usize,
    mode: RsassySearchMode,
) -> Result<Vec<SassyMatch>, String>
where
    Searcher<P>: Clone + Send,
{
    if patterns.is_empty() || texts.is_empty() {
        return Ok(Vec::new());
    }
    if matches!(
        mode,
        RsassySearchMode::BatchPatterns | RsassySearchMode::EncodedPatterns
    ) && patterns
        .iter()
        .any(|pattern| pattern.len() != patterns[0].len())
    {
        return Err(format!(
            "mode '{}' requires all patterns to have the same byte length",
            mode.as_str()
        ));
    }

    // Rayon is useful for native bulk searches. Keep wasm32 builds on the
    // serial path until Rsassy explicitly enables and validates threaded webR
    // execution.
    #[cfg(target_arch = "wasm32")]
    let _ = threads;
    #[cfg(not(target_arch = "wasm32"))]
    if threads > 1 {
        if let Ok(pool) = ThreadPoolBuilder::new().num_threads(threads).build() {
            return Ok(pool.install(|| {
                parallel_search_many(searcher, patterns, texts, k, all_matches, mode)
            }));
        }
    }

    Ok(serial_search_many(
        searcher,
        patterns,
        texts,
        k,
        all_matches,
        mode,
    ))
}
