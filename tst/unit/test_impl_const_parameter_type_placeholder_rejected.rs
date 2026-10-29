//@ compile-fail: the placeholder `_` is not allowed in impl signatures
trait Marker {}
impl<const N: _> Marker for [u8; N] {}
fn main() {}
