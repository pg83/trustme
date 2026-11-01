//@ aux-build: namespace_foundation.rs
//@ aux-build: definition_site_namespaces.rs
#![feature(decl_macro)]

const LIMIT: u16 = 101;
static BIAS: u16 = 103;
fn scale(_: u16) -> u16 { 0 }

fn main() {
    assert_eq!(definition_site_namespaces::values!(), (31, 7, 22));
    assert_eq!((LIMIT, BIAS, scale(11)), (101, 103, 0));
}
