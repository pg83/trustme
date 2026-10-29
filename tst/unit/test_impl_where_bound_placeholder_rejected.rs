//@ compile-fail: the placeholder `_` is not allowed in impl signatures
trait Marker {}
trait Pick<T> {}
impl<T> Marker for T where T: Pick<_> {}
fn main() {}
