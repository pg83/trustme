//@ compile-fail: different source
struct Pair<T, U>(T, U);
impl<T> Pair<T, T> { fn width(&self) -> usize { std::mem::size_of::<T>() } }
fn opaque_pair() -> (impl Copy, impl Copy) { (3u8, 5u8) }
fn main() { let (a, b) = opaque_pair(); let _ = Pair::width(&Pair(a, b)); }
