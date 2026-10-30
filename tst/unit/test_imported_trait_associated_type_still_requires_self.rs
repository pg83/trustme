//@ compile-fail: not allowed in function signatures
#![feature(import_trait_associated_functions)]
trait Source { type Item; }
impl Source for u8 { type Item = u16; }
use Source::Item;
fn take(_: Item) {}
fn main() { take(17u16); }
