//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

trait Duplicate {
    #[edits::passthrough]
    type Item: Copy;
    fn duplicate(value: &Self::Item) -> Self::Item { value.clone() }
}

struct Value;
impl Duplicate for Value { type Item = u16; }

fn main() { assert_eq!(Value::duplicate(&113), 113); }
