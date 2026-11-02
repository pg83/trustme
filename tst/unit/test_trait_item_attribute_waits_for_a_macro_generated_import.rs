//@ proc-macro-aux-build: trait_attribute_edits.rs

trait Read {
    #[edits::expand_to(fn read(&self) -> u16 { 149 })]
    fn read(&self) -> u16;
}

macro_rules! imports { () => { use trait_attribute_edits as edits; }; }
imports!();

struct Value;
impl Read for Value {}

fn main() { assert_eq!(Value.read(), 149); }
