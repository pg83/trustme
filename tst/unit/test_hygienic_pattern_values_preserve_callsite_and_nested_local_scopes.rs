//@ aux-build: hygienic_pattern_payloads.rs
//@ compile-variant: -O
#![feature(decl_macro)]

fn main() {
    let captured = 349u16;
    assert_eq!(hygienic_pattern_payloads::read!(captured), 351);
    {
        let captured = 353u16;
        assert_eq!(hygienic_pattern_payloads::read!(captured), 355);
    }
    assert_eq!(captured, 349);
}
