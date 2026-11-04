//@ aux-build: hygienic_local_item_priority.rs
//@ compile-variant: -O
#![feature(decl_macro)]

fn main() {
    assert_eq!(hygienic_local_item_priority::invoke!(367u16), 359);
}
