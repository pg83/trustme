//@ compile-fail: non-structural
#![feature(type_alias_impl_trait)]
type Key = impl Copy + PartialEq + Eq;
#[define_opaque(Key)]
const KEY: Key = 17u8;
#[derive(PartialEq, Eq)]
struct Record { value: Key }
const RECORD: Record = Record { value: KEY };
fn matches(value: Record) -> bool { match value { RECORD => true, _ => false } }
fn main() { let _ = matches(RECORD); }
