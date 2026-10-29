//@ compile-fail: `impl Trait for .. {}` is an obsolete syntax
trait Marker {}
impl Marker for .. {}
fn main() {}
