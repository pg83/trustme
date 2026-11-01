//@ compile-fail: Could not find macro no_such_reexported_macro_for_this_unit
#[macro_reexport(no_such_reexported_macro_for_this_unit)]
extern crate std as dependency;
fn main() {}
