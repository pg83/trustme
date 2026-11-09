//@ compile-fail: on incorrect item type
#![feature(lang_items)]

#[lang = "sized"]
fn not_a_trait() {}

fn main() {}
