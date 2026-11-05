//@ aux-build: builtin_reexport_origin.rs
//@ aux-build: builtin_reexport_hop.rs
//@ compile-fail: error
#[macro_use(enabled, absent_gate_after_a_real_export)]
extern crate builtin_reexport_hop;

fn main() {}
