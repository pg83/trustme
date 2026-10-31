//@ compile-fail: Type mismatch
fn main() { let object: &dyn Send = "text"; let _ = object; }
