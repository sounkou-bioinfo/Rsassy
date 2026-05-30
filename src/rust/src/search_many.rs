#[cfg(not(target_arch = "wasm32"))]
use rayon::{ThreadPoolBuilder, prelude::*};
use sassy::profiles::Profile;
use sassy::{Match as SassyMatch, Searcher};
use std::os::raw::c_char;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum RsassySearchStrategy {
    Pairwise,
    BatchPatterns,
    BatchTexts,
    EncodedPatterns,
}

impl RsassySearchStrategy {
    fn as_str(self) -> &'static str {
        match self {
            RsassySearchStrategy::Pairwise => "pairwise",
            RsassySearchStrategy::BatchPatterns => "batch_patterns",
            RsassySearchStrategy::BatchTexts => "batch_texts",
            RsassySearchStrategy::EncodedPatterns => "encoded_patterns",
        }
    }
}

pub(crate) fn parse_search_strategy(strategy: *const c_char) -> Result<RsassySearchStrategy, String> {
    let strategy = crate::cstr_arg(strategy, "strategy")?;
    match strategy {
        "pairwise" => Ok(RsassySearchStrategy::Pairwise),
        "batch_patterns" => Ok(RsassySearchStrategy::BatchPatterns),
        "batch_texts" => Ok(RsassySearchStrategy::BatchTexts),
        "encoded_patterns" | "v2" => Ok(RsassySearchStrategy::EncodedPatterns),
        _ => Err(format!(
            "unsupported search strategy: {strategy}; expected 'pairwise', 'batch_texts', 'batch_patterns', 'encoded_patterns', or 'v2'"
        )),
    }
}

fn serial_search_many<P: Profile>(
    searcher: &mut Searcher<P>,
    patterns: &[&[u8]],
    texts: &[&[u8]],
    k: usize,
    all_matches: bool,
    strategy: RsassySearchStrategy,
) -> Vec<SassyMatch> {
    if patterns.is_empty() || texts.is_empty() {
        return Vec::new();
    }

    if all_matches || strategy == RsassySearchStrategy::Pairwise {
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

    match strategy {
        RsassySearchStrategy::Pairwise => unreachable!(),
        RsassySearchStrategy::BatchPatterns => {
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
        RsassySearchStrategy::BatchTexts => {
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
        RsassySearchStrategy::EncodedPatterns => {
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
const CARTESIAN_TASKS_PER_CHUNK: usize = 64;

#[cfg(not(target_arch = "wasm32"))]
fn parallel_search_cartesian<P: Profile>(
    searcher: &Searcher<P>,
    patterns: &[&[u8]],
    texts: &[&[u8]],
    k: usize,
    all_matches: bool,
) -> Vec<SassyMatch>
where
    Searcher<P>: Clone + Send,
{
    let n_texts = texts.len();
    let pair_count = patterns.len() * n_texts;
    let chunk_count = pair_count.div_ceil(CARTESIAN_TASKS_PER_CHUNK);

    let mut chunks: Vec<(usize, Vec<SassyMatch>)> = (0..chunk_count)
        .into_par_iter()
        .map(|chunk_idx| {
            let start = chunk_idx * CARTESIAN_TASKS_PER_CHUNK;
            let end = (start + CARTESIAN_TASKS_PER_CHUNK).min(pair_count);
            let mut local = searcher.clone();
            let mut out = Vec::new();

            for pair_idx in start..end {
                let pattern_idx = pair_idx / n_texts;
                let text_idx = pair_idx % n_texts;
                let mut matches = if all_matches {
                    local.search_all(patterns[pattern_idx], texts[text_idx], k)
                } else {
                    local.search(patterns[pattern_idx], texts[text_idx], k)
                };
                for m in &mut matches {
                    m.pattern_idx = pattern_idx;
                    m.text_idx = text_idx;
                }
                out.extend(matches);
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

#[cfg(not(target_arch = "wasm32"))]
fn parallel_search_many<P: Profile>(
    searcher: &Searcher<P>,
    patterns: &[&[u8]],
    texts: &[&[u8]],
    k: usize,
    all_matches: bool,
    strategy: RsassySearchStrategy,
) -> Vec<SassyMatch>
where
    Searcher<P>: Clone + Send,
{
    match strategy {
        RsassySearchStrategy::Pairwise => {
            parallel_search_cartesian(searcher, patterns, texts, k, all_matches)
        }
        RsassySearchStrategy::BatchPatterns => texts
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
        RsassySearchStrategy::BatchTexts => patterns
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
        RsassySearchStrategy::EncodedPatterns => {
            let mut local = searcher.clone();
            search_encoded_patterns_many(&mut local, patterns, texts, k, all_matches)
        }
    }
}

pub(crate) fn validate_strategy_for_profile(
    alphabet: &str,
    strategy: RsassySearchStrategy,
) -> Result<(), String> {
    if matches!(
        strategy,
        RsassySearchStrategy::BatchPatterns | RsassySearchStrategy::EncodedPatterns
    ) && alphabet != "iupac"
    {
        return Err(format!(
            "strategy '{}' uses Sassy multi-pattern encoding and requires alphabet = 'iupac'",
            strategy.as_str()
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
    strategy: RsassySearchStrategy,
) -> Result<Vec<SassyMatch>, String>
where
    Searcher<P>: Clone + Send,
{
    if patterns.is_empty() || texts.is_empty() {
        return Ok(Vec::new());
    }
    if all_matches && strategy != RsassySearchStrategy::Pairwise {
        return Err(format!(
            "all = TRUE returns every end position with score <= k and requires strategy = 'pairwise', not '{}'",
            strategy.as_str()
        ));
    }
    if matches!(
        strategy,
        RsassySearchStrategy::BatchPatterns | RsassySearchStrategy::EncodedPatterns
    ) && patterns
        .iter()
        .any(|pattern| pattern.len() != patterns[0].len())
    {
        return Err(format!(
            "strategy '{}' requires all patterns to have the same byte length",
            strategy.as_str()
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
                parallel_search_many(searcher, patterns, texts, k, all_matches, strategy)
            }));
        }
    }

    Ok(serial_search_many(
        searcher,
        patterns,
        texts,
        k,
        all_matches,
        strategy,
    ))
}
