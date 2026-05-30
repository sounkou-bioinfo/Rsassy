use std::cell::RefCell;
use std::ffi::CString;
use std::os::raw::c_char;

thread_local! {
    static LAST_FEATURES: RefCell<Option<CString>> = const { RefCell::new(None) };
}

fn bool_text(value: bool) -> &'static str {
    if value { "true" } else { "false" }
}

fn build_features_string() -> String {
    let compiled_avx2 = cfg!(target_feature = "avx2");
    let compiled_avx512f = cfg!(target_feature = "avx512f");
    let compiled_neon = cfg!(target_feature = "neon");
    let compiled_wasm_simd128 = cfg!(target_feature = "simd128");
    let simd_backend = if compiled_wasm_simd128 {
        "wasm_simd128"
    } else if compiled_avx512f {
        "avx512f"
    } else if compiled_avx2 {
        "avx2"
    } else if compiled_neon {
        "neon"
    } else {
        "portable_scalar"
    };

    let mut out = String::new();
    out.push_str(&format!(
        "rsassy_rust_version: {}\n",
        env!("CARGO_PKG_VERSION")
    ));
    out.push_str(&format!("target_arch: {}\n", std::env::consts::ARCH));
    out.push_str(&format!("target_os: {}\n", std::env::consts::OS));
    out.push_str(&format!("selected_simd_backend: {simd_backend}\n"));
    out.push_str(&format!(
        "selected_portable_scalar: {}\n",
        bool_text(simd_backend == "portable_scalar")
    ));
    out.push_str(&format!(
        "selected_native_simd: {}\n",
        bool_text(cfg!(feature = "native-simd"))
    ));
    out.push_str(&format!(
        "selected_compiled_avx2: {}\n",
        bool_text(compiled_avx2)
    ));
    out.push_str(&format!(
        "selected_compiled_avx512f: {}\n",
        bool_text(compiled_avx512f)
    ));
    out.push_str(&format!(
        "selected_compiled_neon: {}\n",
        bool_text(compiled_neon)
    ));
    out.push_str(&format!(
        "selected_compiled_wasm_simd128: {}\n",
        bool_text(compiled_wasm_simd128)
    ));
    out
}

#[no_mangle]
pub extern "C" fn rsassy_features_string() -> *const c_char {
    let features = build_features_string().replace('\0', "\\0");
    LAST_FEATURES.with(|slot| {
        *slot.borrow_mut() = Some(CString::new(features).expect("interior NULs were replaced"));
        slot.borrow().as_ref().unwrap().as_ptr()
    })
}
