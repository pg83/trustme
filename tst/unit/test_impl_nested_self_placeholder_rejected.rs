//@ compile-fail: the placeholder `_` is not allowed in impl signatures
trait Mark {}
impl Mark for (u8, Option<_>) {}
fn main() {}
