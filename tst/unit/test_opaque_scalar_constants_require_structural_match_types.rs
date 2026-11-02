//@ compile-fail: non-structural
#![feature(type_alias_impl_trait)]
type Key = impl Copy + PartialEq;
#[define_opaque(Key)]
const KEY: Key = 17u8;
fn matches(value: Key) -> bool { match value { KEY => true, _ => false } }
fn main() { let _ = matches(KEY); }
