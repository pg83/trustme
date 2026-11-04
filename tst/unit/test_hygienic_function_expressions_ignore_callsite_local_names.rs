//@ aux-build: hygienic_pattern_payloads.rs
//@ compile-variant: -O
#![feature(decl_macro)]

fn main() {
    let original = 257u16;
    assert_eq!(hygienic_pattern_payloads::invoke!(263u16), 266);
    assert_eq!(original, 257);
}
