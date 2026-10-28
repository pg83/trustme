#![feature(type_alias_impl_trait)]

struct Pair<T, U>(T, U);
impl<T> Pair<T, T> {
    fn width(&self) -> usize { std::mem::size_of::<T>() }
}

fn opaque<T: Copy>(value: T) -> impl Copy { value }
type Hidden<T: Copy> = impl Copy;
#[define_opaque(Hidden)]
fn hidden<T: Copy>(value: T) -> Hidden<T> { value }

fn main() {
    assert_eq!(Pair(opaque(3u8), opaque(5u8)).width(), 1);
    assert_eq!(Pair(opaque(7u16), opaque(11u16)).width(), 2);
    assert_eq!(Pair(hidden(13u8), hidden(17u8)).width(), 1);
    assert_eq!(Pair(hidden(19u32), hidden(23u32)).width(), 4);
}
