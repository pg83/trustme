//@ compile-fail: the placeholder `_` is not allowed in function signatures
trait Code { fn code() -> u32; }
impl Code for () { fn code() -> _ { 7u32 } }
fn main() {}
