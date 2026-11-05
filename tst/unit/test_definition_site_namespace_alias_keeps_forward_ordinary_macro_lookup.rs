//@ edition: 2018
//@ aux-build: opaque_namespace_route.rs
#![feature(decl_macro)]

opaque_namespace_route::home::route!(Imported);

const LOCAL: u16 = later::constant!();
mod later {
    pub macro constant() { 613u16 }
}

fn main() {
    assert_eq!(LOCAL, 613);
    assert_eq!(Imported::value!(), 607);
    assert_eq!(opaque_namespace_route::home::nested::value!(), 607);
}
