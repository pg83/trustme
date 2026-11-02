//@ compile-fail: Drop can only be implemented for structs, enums, and unions
struct Local;
impl Drop for [Local] { fn drop(&mut self) {} }
fn main() {}
