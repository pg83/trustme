struct Pair<T, U>(T, U);
impl<T> Pair<T, T> { fn width(&self) -> usize { std::mem::size_of::<T>() } }
fn opaque<T: Copy>(value: T) -> impl Copy { value }
fn positions() -> (impl Copy, impl Copy) { (13u8, 17u32) }
fn main() {
    assert_eq!(Pair::width(&Pair(opaque(3u8), opaque(5u8))), 1);
    assert_eq!(Pair::width(&Pair(opaque(7u32), opaque(11u32))), 4);
    let (a, b) = positions();
    assert_eq!(Pair::width(&Pair(a, a)), 1);
    assert_eq!(Pair::width(&Pair(b, b)), 4);
}
