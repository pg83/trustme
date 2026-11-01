//@ aux-build: nullable_star_matcher.rs
//@ compile-fail: ambigu
fn main() { let _: [u16; 0] = nullable_star_matcher::sizes!(); }
