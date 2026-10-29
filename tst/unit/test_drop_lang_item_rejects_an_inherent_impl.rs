//@ compile-fail: Unknown lang item 'drop' on impl
#![feature(lang_items)]
struct Resource;
#[lang = "drop"]
impl Resource { fn code(&self) -> u8 { 7 } }
fn main() { assert_eq!(Resource.code(), 7); }
