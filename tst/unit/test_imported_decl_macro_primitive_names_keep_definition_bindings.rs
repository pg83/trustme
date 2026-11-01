//@ aux-build: primitive_import_macros.rs
#![feature(decl_macro)]
#![allow(non_camel_case_types)]
extern crate primitive_import_macros as source;
fn main() {
    struct u16 { value: bool }
    struct u32 { value: bool }
    assert_eq!(source::primitive_word!(), 257);
    assert_eq!(source::definition::record!(), 127);
    assert_eq!(source::definition::constant_word!(), 131);
}
