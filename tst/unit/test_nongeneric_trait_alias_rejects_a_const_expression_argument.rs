//@ compile-fail: generic
#![feature(trait_alias)]
trait Plain = Copy;
fn consume<T: Plain<{ 7 + 8 }>>(_: T) {}
fn main() { consume(3u8); }
