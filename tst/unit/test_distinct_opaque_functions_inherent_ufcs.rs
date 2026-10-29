//@ compile-fail: different source
struct Pair<T, U>(T, U);
impl<T> Pair<T, T> { fn width(&self) -> usize { std::mem::size_of::<T>() } }
fn opaque<T: Copy>(value: T) -> impl Copy { value }
fn other(value: u8) -> impl Copy { value }
fn main() { let _ = Pair::width(&Pair(opaque(3u8), other(5))); }
