//@ compile-fail: error
trait Width<T> { const VALUE: usize; }
struct Source;
impl Width<u8> for Source { const VALUE: usize = 2; }
impl Width<u16> for Source { const VALUE: usize = 3; }
struct Buffer([u8; <Source as Width<_>>::VALUE]);
fn main() {}
