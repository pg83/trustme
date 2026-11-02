//@ proc-macro-aux-build: trait_attribute_edits.rs
#![feature(associated_type_defaults)]
extern crate trait_attribute_edits as edits;

trait Choice {
    #[edits::expand_to(type Chosen = u32;)]
    type Chosen;
    fn answer() -> Self::Chosen;
}

struct Value;
impl Choice for Value {
    fn answer() -> u32 { 109 }
}

fn main() {
    let answer: <Value as Choice>::Chosen = Value::answer();
    assert_eq!(answer, 109u32);
}
