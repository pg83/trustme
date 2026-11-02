//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

trait Amount {
    #[edits::expand_to(const AMOUNT: u16 = 73;)]
    const AMOUNT: u16 = 1;
}

struct Value;
impl Amount for Value {}

fn main() { assert_eq!(Value::AMOUNT, 73); }
