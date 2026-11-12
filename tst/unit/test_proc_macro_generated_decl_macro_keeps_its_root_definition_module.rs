//@ proc-macro-aux-build: own_root_decl_macro.rs
#![feature(decl_macro)]

extern crate own_root_decl_macro;

fn own_source() -> u16 {
    41
}

own_root_decl_macro::declare_reader!();

fn main() {
    let own_source = || 99u16;
    assert_eq!(own_reader!(), 41);
    assert_eq!(own_source(), 99);
}
