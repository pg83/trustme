//@ compile-fail: the placeholder `_` is not allowed in impl signatures
trait Marker {}
impl Marker for _ {}
fn need<T: Marker>(_: T) {}
fn main() { need(3u8); }
