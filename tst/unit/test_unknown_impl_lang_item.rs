//@ compile-fail: Unknown lang item 'missing_impl_lang_item' on impl
#![feature(lang_items)]

struct Example;

#[lang = "missing_impl_lang_item"]
impl Example {}

fn main() {}
