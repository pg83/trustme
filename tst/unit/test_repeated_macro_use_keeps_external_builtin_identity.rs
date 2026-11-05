//@ aux-build: builtin_reexport_origin.rs
//@ aux-build: builtin_reexport_hop.rs
//@ compile-variant: -O
#[macro_use]
extern crate builtin_reexport_hop as first;
#[macro_use]
extern crate builtin_reexport_hop as second;

fn main() {
    assert!(enabled!(not(any())));
    assert!(!nested_enabled!(not(all())));
}
