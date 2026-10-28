//@ compile-fail: Wildcard import of invalid item type
fn value() {}
use value::*;
fn main() {}
