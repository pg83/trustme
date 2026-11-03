//@ aux-build: selective_macro_exports.rs
//@ compile-fail: Couldn't find macro
#[macro_use(no_such_selected_macro)]
extern crate selective_macro_exports;
fn main() {}
