//@ compile-fail: cycle detected
use std::cell::Cell;
struct Container { value: Option<Cell<u16>> }
const FIRST: Container = Container { value: SECOND.value };
const SECOND: Container = Container { value: FIRST.value };
const VALUE: &Option<Cell<u16>> = &FIRST.value;
fn main() { let _ = VALUE; }
