//@ aux-build: private_macro_origin_for_imports.rs
//@ aux-build: local_private_macro_import_bridge.rs
//@ aux-build: external_private_macro_import_bridge.rs
//@ compile-variant: -O

#[macro_use]
extern crate local_private_macro_import_bridge;
#[macro_use]
extern crate external_private_macro_import_bridge;

fn main() {
    assert_eq!(opened_local_seed!(), 59u16);
    assert_eq!(opened_external_seed!(), 67u16);
}
