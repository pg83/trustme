//@ compile-fail: the placeholder `_` is not allowed in function signatures
trait Shape { fn size() -> _; }
struct Unit;
impl Shape for Unit { fn size() -> u32 { 7 } }
fn main() {}
