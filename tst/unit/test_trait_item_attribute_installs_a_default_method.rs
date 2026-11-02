//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

trait Read {
    #[edits::expand_to(fn computed(&self) -> u16 { 71 })]
    fn computed(&self) -> u16;
}

struct Value;
impl Read for Value {}

fn main() { assert_eq!(Value.computed(), 71); }
