//@ compile-fail: Unknown language item
#![feature(lang_items)]
#[lang = "coverage_unknown_static"]
static VALUE: u16 = 323;
fn main() { assert_eq!(VALUE, 323); }
