//@ compile-fail: Drop can only be implemented for structs, enums, and unions
struct Local;
impl<const N: usize> Drop for [Local; N] { fn drop(&mut self) {} }
fn main() {}
