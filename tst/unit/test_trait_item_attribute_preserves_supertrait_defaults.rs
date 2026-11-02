//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

trait Base { fn base(&self) -> u16 { 157 } }

#[edits::passthrough]
trait Derived: Base {
    #[edits::expand_to(fn own(&self) -> u16 { self.base() + 6 })]
    fn own(&self) -> u16;
}

struct Value;
impl Base for Value {}
impl Derived for Value {}

fn main() { assert_eq!(Value.own(), 163); }
