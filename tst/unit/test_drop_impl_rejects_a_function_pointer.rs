//@ compile-fail: Drop can only be implemented for structs, enums, and unions
impl Drop for fn() { fn drop(&mut self) {} }
fn main() {}
