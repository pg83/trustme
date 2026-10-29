//@ compile-fail: the placeholder `_` is not allowed in impl signatures
trait Marker {}
impl<T> Marker for T where Option<_>: Copy {}
fn main() {}
