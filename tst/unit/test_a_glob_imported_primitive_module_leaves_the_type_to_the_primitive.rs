//@ aux-build: glob_primitive_modules.rs
// path_abs does `use std_prelude::*;`, and std_prelude re-exports the modules
// `std::str`, `std::u8`, `std::f64`. Upstream resolves a path in type position
// that lands on a module, when its first segment names a primitive type, to
// that primitive (`resolve_qpath`, rustc_resolve late.rs: "use std::u8; fn f()
// -> u8"); we kept the glob-imported module `alloc::str` as the type of
// `Cow<'_, str>`.
use glob_primitive_modules::*;

fn lossy(text: &str) -> Cow<'_, str> {
    Cow::Borrowed(text)
}

fn largest() -> u8 {
    u8::max_value()
}

fn half(x: f64) -> f64 {
    x / 2.0
}

fn main() {
    assert_eq!(lossy("abc"), "abc");
    assert_eq!(largest(), 255);
    assert_eq!(half(3.0), 1.5);
    assert_eq!(str::from_utf8(b"ok").unwrap(), "ok");
}
