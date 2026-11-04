//@ aux-build: hygienic_pattern_payloads.rs
//@ compile-variant: -O
#![feature(decl_macro)]

fn main() {
    assert_eq!(hygienic_pattern_payloads::read!(239u16), 241);
    assert_eq!(hygienic_pattern_payloads::read!(251u16), 253);
}
