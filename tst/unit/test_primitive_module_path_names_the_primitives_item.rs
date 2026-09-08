//@ run-pass
/* rustix's `cstr!`: `::core::iter::Iterator::any(&mut ::core::primitive::str::bytes($str), |b| b ==
   b'\0')` names a method of `str` through `core::primitive`, whose `pub use str;` resolves to
   the builtin type: the rest of the path is the primitive's item, as a bare `str::bytes` is.
   The import's binding named the type alone, and the path that went on from it had none
   (`resolve_main_bindings.cpp:942 path.bindings.hasBinding()`). */
macro_rules! cstr {
    ($str:literal) => {{
        ::core::assert!(
            !::core::iter::Iterator::any(&mut ::core::primitive::str::bytes($str), |b| b == b'\0'),
            "cstr argument contains embedded NUL bytes",
        );
        #[allow(unsafe_code, unused_unsafe)]
        {
            unsafe { ::core::ffi::CStr::from_bytes_with_nul_unchecked(::core::concat!($str, "\0").as_bytes()) }
        }
    }};
}

fn main() {
    let s = cstr!(".");
    assert_eq!(s.to_bytes(), b".");
    let n = ::core::primitive::str::len("abc");
    assert_eq!(n, 3);
}
