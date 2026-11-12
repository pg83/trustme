//@ aux-build: own_imported_macro_item_scope.rs
extern crate own_imported_macro_item_scope as source;

fn value() -> u16 { 97 }

fn main() {
    assert_eq!(source::answer!(), 61);
    assert_eq!(value(), 97);
}
