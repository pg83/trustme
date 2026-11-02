//@ compile-fail: Drop can only be implemented for structs, enums, and unions
trait Local {}
impl Drop for dyn Local { fn drop(&mut self) {} }
fn main() {}
