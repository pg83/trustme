//@ aux-build: namespace_foundation.rs
//@ aux-build: definition_site_namespaces.rs
#![feature(decl_macro)]

fn main() {
    assert_eq!(definition_site_namespaces::remote_imports!(), (41, 43));
}
