//@ compile-fail: Type mismatch
fn erase<T: Send + ?Sized>(source: &T) -> &dyn Send { source }
fn main() { let _ = erase(&17u32); }
