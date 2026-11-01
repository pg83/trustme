//@ aux-build: mutable_definition_site.rs
#![feature(decl_macro)]

static mut COUNTER: u16 = 101;

fn main() {
    assert_eq!(mutable_definition_site::increment!(), 8);
    assert_eq!(mutable_definition_site::increment!(), 9);
    assert_eq!(unsafe { COUNTER }, 101);
}
