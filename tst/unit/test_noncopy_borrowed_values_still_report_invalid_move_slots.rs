//@ compile-fail: move out
fn take(value: &mut String) -> String { *value }
fn main() { let _ = take(&mut String::from("borrowed")); }
