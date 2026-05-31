use needletail::{parse_fastx_file, parser::FastxReader};
use std::ffi::CStr;
use std::os::raw::{c_char, c_int};
use std::path::PathBuf;
use std::ptr;

#[cfg(unix)]
use std::os::unix::ffi::OsStrExt;

use crate::guard_ffi;

pub struct RsassyFastxIter {
    reader: Box<dyn FastxReader>,
    batch_records: usize,
    include_qual: bool,
}

pub struct RsassyFastxBatch {
    id_buffer: Vec<u8>,
    id_offsets: Vec<usize>,
    id_lens: Vec<usize>,
    id_utf8: Vec<bool>,
    seq_buffer: Vec<u8>,
    seq_offsets: Vec<usize>,
    seq_lens: Vec<usize>,
    qual_buffer: Vec<u8>,
    qual_offsets: Vec<usize>,
    qual_lens: Vec<usize>,
    has_qual: bool,
}

impl RsassyFastxBatch {
    fn with_capacity(records: usize) -> Self {
        // Conservative defaults for short-read FASTQ. Vectors still grow for
        // long reads; the public API intentionally bounds batches by records.
        let id_capacity = records.saturating_mul(32);
        let seq_capacity = records.saturating_mul(150);
        Self {
            id_buffer: Vec::with_capacity(id_capacity),
            id_offsets: Vec::with_capacity(records),
            id_lens: Vec::with_capacity(records),
            id_utf8: Vec::with_capacity(records),
            seq_buffer: Vec::with_capacity(seq_capacity),
            seq_offsets: Vec::with_capacity(records),
            seq_lens: Vec::with_capacity(records),
            qual_buffer: Vec::with_capacity(seq_capacity),
            qual_offsets: Vec::with_capacity(records),
            qual_lens: Vec::with_capacity(records),
            has_qual: false,
        }
    }

    fn n(&self) -> usize {
        self.seq_lens.len()
    }

    fn push_id(&mut self, id: &[u8]) {
        self.id_offsets.push(self.id_buffer.len());
        self.id_lens.push(id.len());
        self.id_utf8.push(std::str::from_utf8(id).is_ok());
        self.id_buffer.extend_from_slice(id);
    }

    fn push_seq(&mut self, seq: &[u8]) {
        self.seq_offsets.push(self.seq_buffer.len());
        self.seq_lens.push(seq.len());
        self.seq_buffer.extend_from_slice(seq);
    }

    fn push_seq_stripped(&mut self, seq: &[u8]) {
        let offset = self.seq_buffer.len();
        self.seq_offsets.push(offset);
        self.seq_buffer
            .extend(seq.iter().copied().filter(|b| *b != b'\n' && *b != b'\r'));
        self.seq_lens.push(self.seq_buffer.len() - offset);
    }

    fn push_qual(&mut self, qual: Option<&[u8]>) {
        match qual {
            Some(qual) => {
                self.has_qual = true;
                self.qual_offsets.push(self.qual_buffer.len());
                self.qual_lens.push(qual.len());
                self.qual_buffer.extend_from_slice(qual);
            }
            None => {
                self.qual_offsets.push(0);
                self.qual_lens.push(0);
            }
        }
    }

    fn push_record(&mut self, record: &needletail::parser::SequenceRecord<'_>, include_qual: bool) {
        self.push_id(record.id());
        // `SequenceRecord::seq()` allocates for wrapped FASTA so it can remove
        // line endings. Avoid that per-record allocation by borrowing the raw
        // sequence and copying into the batch slab while stripping CR/LF.
        let raw_seq = record.raw_seq();
        if raw_seq.iter().any(|b| *b == b'\n' || *b == b'\r') {
            self.push_seq_stripped(raw_seq);
        } else {
            self.push_seq(raw_seq);
        }
        self.push_qual(if include_qual { record.qual() } else { None });
    }
}

fn path_buf_from_cstr(path: *const c_char) -> Result<PathBuf, String> {
    if path.is_null() {
        return Err("path pointer must not be NULL".to_string());
    }
    #[cfg(unix)]
    {
        // SAFETY: `path` was checked for NULL and is expected to point to a
        // NUL-terminated R string for the duration of this FFI call. On Unix,
        // paths are byte strings, so preserve R's bytes rather than requiring
        // UTF-8 at the Rust boundary.
        let bytes = unsafe { CStr::from_ptr(path) }.to_bytes();
        Ok(std::ffi::OsStr::from_bytes(bytes).to_owned().into())
    }
    #[cfg(not(unix))]
    {
        Ok(PathBuf::from(crate::cstr_arg(path, "path")?))
    }
}

fn slice_ptr(buffer: &[u8], offsets: &[usize], lens: &[usize], index: usize) -> *const u8 {
    if index >= offsets.len() || index >= lens.len() {
        return ptr::null();
    }
    let offset = offsets[index];
    let len = lens[index];
    if len == 0 {
        return ptr::null();
    }
    if offset > buffer.len() || len > buffer.len().saturating_sub(offset) {
        return ptr::null();
    }
    // SAFETY: Bounds are checked above and the returned pointer borrows the
    // immutable batch slab for as long as the batch object is alive.
    unsafe { buffer.as_ptr().add(offset) }
}

fn slice_len(offsets: &[usize], lens: &[usize], index: usize) -> usize {
    if index >= offsets.len() || index >= lens.len() {
        0
    } else {
        lens[index]
    }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_iter_new(
    path: *const c_char,
    batch_records: usize,
    include_qual: bool,
    out: *mut *mut RsassyFastxIter,
) -> c_int {
    guard_ffi(|| {
        if out.is_null() {
            return Err("out FASTX iterator pointer must not be NULL".to_string());
        }
        if batch_records == 0 {
            return Err("batch_records must be >= 1".to_string());
        }
        let path = path_buf_from_cstr(path)?;
        let reader = parse_fastx_file(path).map_err(|err| err.to_string())?;
        let iter = Box::new(RsassyFastxIter {
            reader,
            batch_records,
            include_qual,
        });
        // SAFETY: `out` was checked for NULL above and receives ownership of
        // the boxed iterator. The C/R finalizer calls `rsassy_fastx_iter_free`.
        unsafe {
            *out = Box::into_raw(iter);
        }
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_iter_free(iter: *mut RsassyFastxIter) {
    if !iter.is_null() {
        // SAFETY: The pointer must have been returned by
        // `rsassy_fastx_iter_new` and is consumed exactly once by the finalizer.
        unsafe {
            drop(Box::from_raw(iter));
        }
    }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_iter_next(
    iter: *mut RsassyFastxIter,
    out: *mut *mut RsassyFastxBatch,
) -> c_int {
    guard_ffi(|| {
        if iter.is_null() {
            return Err("FASTX iterator pointer must not be NULL".to_string());
        }
        if out.is_null() {
            return Err("out FASTX batch pointer must not be NULL".to_string());
        }
        // SAFETY: `iter` was checked for NULL and is owned by the R external
        // pointer for this synchronous call.
        let iter = unsafe { &mut *iter };
        let mut batch = RsassyFastxBatch::with_capacity(iter.batch_records);
        while batch.n() < iter.batch_records {
            let Some(record) = iter.reader.next() else {
                break;
            };
            let record = record.map_err(|err| err.to_string())?;
            batch.push_record(&record, iter.include_qual);
        }
        // SAFETY: `out` was checked for NULL. NULL marks end-of-file.
        unsafe {
            if batch.n() == 0 {
                *out = ptr::null_mut();
            } else {
                *out = Box::into_raw(Box::new(batch));
            }
        }
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_batch_free(batch: *mut RsassyFastxBatch) {
    if !batch.is_null() {
        // SAFETY: The pointer must have been returned by
        // `rsassy_fastx_iter_next` and is consumed exactly once by the R batch
        // finalizer.
        unsafe {
            drop(Box::from_raw(batch));
        }
    }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_batch_n(batch: *const RsassyFastxBatch) -> usize {
    if batch.is_null() {
        0
    } else {
        // SAFETY: Non-NULL batch pointers are immutable and owned by R.
        unsafe { (*batch).n() }
    }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_batch_has_qual(batch: *const RsassyFastxBatch) -> bool {
    !batch.is_null() && unsafe { (*batch).has_qual }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_batch_id_ptr(
    batch: *const RsassyFastxBatch,
    index: usize,
) -> *const u8 {
    if batch.is_null() {
        ptr::null()
    } else {
        let batch = unsafe { &*batch };
        slice_ptr(&batch.id_buffer, &batch.id_offsets, &batch.id_lens, index)
    }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_batch_id_len(batch: *const RsassyFastxBatch, index: usize) -> usize {
    if batch.is_null() {
        0
    } else {
        let batch = unsafe { &*batch };
        slice_len(&batch.id_offsets, &batch.id_lens, index)
    }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_batch_id_utf8(batch: *const RsassyFastxBatch, index: usize) -> bool {
    if batch.is_null() {
        false
    } else {
        let batch = unsafe { &*batch };
        batch.id_utf8.get(index).copied().unwrap_or(false)
    }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_batch_seq_ptr(
    batch: *const RsassyFastxBatch,
    index: usize,
) -> *const u8 {
    if batch.is_null() {
        ptr::null()
    } else {
        let batch = unsafe { &*batch };
        slice_ptr(
            &batch.seq_buffer,
            &batch.seq_offsets,
            &batch.seq_lens,
            index,
        )
    }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_batch_seq_len(
    batch: *const RsassyFastxBatch,
    index: usize,
) -> usize {
    if batch.is_null() {
        0
    } else {
        let batch = unsafe { &*batch };
        slice_len(&batch.seq_offsets, &batch.seq_lens, index)
    }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_batch_qual_ptr(
    batch: *const RsassyFastxBatch,
    index: usize,
) -> *const u8 {
    if batch.is_null() {
        ptr::null()
    } else {
        let batch = unsafe { &*batch };
        slice_ptr(
            &batch.qual_buffer,
            &batch.qual_offsets,
            &batch.qual_lens,
            index,
        )
    }
}

#[no_mangle]
pub extern "C" fn rsassy_fastx_batch_qual_len(
    batch: *const RsassyFastxBatch,
    index: usize,
) -> usize {
    if batch.is_null() {
        0
    } else {
        let batch = unsafe { &*batch };
        slice_len(&batch.qual_offsets, &batch.qual_lens, index)
    }
}
