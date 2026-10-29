#![feature(type_alias_impl_trait)]
struct Pair<A, B>(A, B);
trait Same { fn width(&self) -> usize; }
impl<T: Copy> Same for Pair<T, T> {
    fn width(&self) -> usize { std::mem::size_of::<T>() }
}
fn opaque<T: Copy>(value: T) -> impl Copy { value }
type Bytes<const N: usize> = impl Copy;
#[define_opaque(Bytes)]
fn bytes<const N: usize>() -> Bytes<N> { [13u8; N] }
fn main() {
    let functions = Pair(opaque(3u8), opaque(5u8));
    assert_eq!(functions.width(), 1);
    let aliases = Pair(bytes::<3>(), bytes::<3>());
    assert_eq!(aliases.width(), 3);
}
