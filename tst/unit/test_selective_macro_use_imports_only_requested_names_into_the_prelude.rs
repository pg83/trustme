//@ aux-build: selective_macro_exports.rs
//@ reject-flags: --cfg unavailable => Unknown macro || cannot find macro
#[macro_use(kept)]
extern crate selective_macro_exports;
#[cfg(unavailable)]
fn main() { let _ = skipped!(); }
#[cfg(not(unavailable))]
fn main() {
    assert_eq!(kept!(), 181);
    assert_eq!(selective_macro_exports::skipped!(), 191);
}
