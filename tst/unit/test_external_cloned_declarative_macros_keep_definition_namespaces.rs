//@ aux-build: copied_declarative_definition.rs
#![feature(decl_macro)]
extern crate copied_declarative_definition as definitions;

fn main() {
    assert_eq!(definitions::first::inner::offset!(2), 61);
    assert_eq!(definitions::second::inner::offset!(8), 67);
}
