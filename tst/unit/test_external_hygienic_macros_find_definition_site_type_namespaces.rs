//@ aux-build: namespace_foundation.rs
//@ aux-build: definition_site_namespaces.rs
#![feature(decl_macro)]

type Word = u8;
struct Pair(u8);

fn main() {
    assert_eq!(definition_site_namespaces::types!(), (11, 13, 17, 19, 13));
    assert_eq!(core::mem::size_of::<Word>(), 1);
    assert_eq!(Pair(29).0, 29);
}
