//@ compile-fail: Unknown language item
#![feature(lang_items)]
#[lang = "coverage_unknown_alias"]
type Alias = u16;
fn main() { let _: Alias = 319; }
