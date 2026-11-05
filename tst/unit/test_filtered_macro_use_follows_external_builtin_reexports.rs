//@ aux-build: builtin_reexport_origin.rs
//@ aux-build: builtin_reexport_hop.rs
//@ compile-variant: -O
#[macro_use(enabled, nested_enabled)]
extern crate builtin_reexport_hop;

fn main() {
    assert!(enabled!(all()));
    assert!(!enabled!(any()));
    assert!(nested_enabled!(any(all(), any())));
    assert!(!nested_enabled!(all(all(), any())));
}
