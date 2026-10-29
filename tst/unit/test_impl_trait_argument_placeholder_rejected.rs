//@ compile-fail: the placeholder `_` is not allowed in impl signatures
trait Pick<T> {}
impl Pick<_> for u8 {}
fn main() {}
