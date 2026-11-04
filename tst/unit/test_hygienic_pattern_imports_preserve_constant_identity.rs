//@ aux-build: hygienic_pattern_candidates.rs
//@ compile-variant: -O
#![feature(decl_macro)]

fn main() {
    assert!(hygienic_pattern_candidates::imported!(137u16));
    assert!(!hygienic_pattern_candidates::imported!(151u16));
}
