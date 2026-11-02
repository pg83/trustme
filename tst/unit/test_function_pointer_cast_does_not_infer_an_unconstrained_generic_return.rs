//@ compile-fail: type annotations needed
fn extract<T>(value: Option<T>) -> T { value.unwrap() }
fn unused() { let _ = extract(None) as fn() -> u16; }
fn main() {}
