//@ compile-fail: the placeholder `_` is not allowed in pattern range base types
#![feature(pattern_types, pattern_type_macro)]
fn main() {
    let value: std::pat::pattern_type!(_ is 3..=9) = 7u32;
    let raw: u32 = unsafe { std::mem::transmute(value) };
    assert_eq!(raw, 7);
}
