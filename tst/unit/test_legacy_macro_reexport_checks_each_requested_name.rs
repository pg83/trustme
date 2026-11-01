//@ aux-build: legacy_reexport_dependency.rs
//@ compile-fail: Could not find macro no_such_macro_after_the_exported_one
#[macro_reexport(own_exported_macro, no_such_macro_after_the_exported_one)]
extern crate legacy_reexport_dependency;
fn main() {}
