//@ compile-fail: Unexpected `helpers` in `#[proc_macro_derive]`
#![feature(no_core)]
#![no_core]

#[proc_macro_derive(Widget, helpers(prefix))]
fn derive_widget() {}

fn main() {}
