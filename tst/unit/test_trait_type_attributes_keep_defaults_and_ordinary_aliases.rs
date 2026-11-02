//@ proc-macro-aux-build: trait_attribute_edits.rs
#![feature(associated_type_defaults)]
extern crate trait_attribute_edits as edits;

#[edits::passthrough]
type Alias = u16;

#[edits::passthrough]
trait Choice {
    #[edits::passthrough]
    type Item = u32;
    fn answer() -> Self::Item;
}

struct Value;
impl Choice for Value { fn answer() -> u32 { 167 } }

fn main() {
    assert_eq!(std::mem::size_of::<Alias>(), 2);
    let answer: <Value as Choice>::Item = Value::answer();
    assert_eq!(answer, 167u32);
}
