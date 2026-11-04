//@ aux-build: hygienic_pattern_candidates.rs
//@ compile-variant: -O
#![feature(decl_macro)]

fn main() {
    assert_eq!(hygienic_pattern_candidates::trait_binding!(137u16), 149);
    assert_eq!(hygienic_pattern_candidates::trait_binding!(151u16), 149);
}
