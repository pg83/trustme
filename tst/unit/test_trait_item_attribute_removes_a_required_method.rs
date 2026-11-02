//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

trait Optional {
    #[edits::discard]
    fn removed(&self) -> u16;
    fn kept(&self) -> u16 { 79 }
}

struct Value;
impl Optional for Value {}

fn main() { assert_eq!(Value.kept(), 79); }
