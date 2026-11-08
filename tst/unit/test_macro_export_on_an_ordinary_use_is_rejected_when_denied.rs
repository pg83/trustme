//@ compile-fail: #[macro_export] on non-macro_rules
#![deny(unused_attributes)]
#[macro_export]
use std::mem::drop as ordinary_item;
fn main() {}
