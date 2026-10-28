//@ compile-fail: Mismatched bindings in pattern
fn main() { match Some(1) { Some(value) | None => {}, _ => {} } }
