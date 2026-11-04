//@ aux-build: hygienic_pattern_candidates.rs
//@ compile-variant: -O
#![feature(decl_macro)]
#![allow(non_upper_case_globals)]

const fallback: u16 = 193;

fn main() {
    assert_eq!(hygienic_pattern_candidates::function_binding!(193u16), 139);
    assert_eq!(hygienic_pattern_candidates::function_binding!(197u16), 139);
    assert_eq!(fallback, 193);
}
