//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

trait Values {
    #[edits::passthrough]
    #[edits::passthrough]
    #[edits::expand_to(fn value(&self) -> u16 { 89 })]
    fn value(&self) -> u16;

    #[edits::passthrough]
    fn after(&self) -> u16 { 97 }
}

struct Value;
impl Values for Value {}

fn main() { assert_eq!(Value.value() + Value.after(), 186); }
