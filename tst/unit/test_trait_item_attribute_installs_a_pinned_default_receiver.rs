//@ proc-macro-aux-build: trait_attribute_edits.rs
#![feature(pin_ergonomics)]
#![allow(incomplete_features)]
extern crate trait_attribute_edits as edits;

trait Read {
    #[edits::expand_to(fn read(&pin const self) -> u16 {
        let pinned: std::pin::Pin<&Self> = self;
        std::hint::black_box(pinned);
        107
    })]
    fn read(&pin const self) -> u16;
}

struct Value;
impl Read for Value {}

fn main() { assert_eq!(Read::read(&pin const Value), 107); }
