use std::marker::PhantomData;

trait Family<T: ?Sized, const N: usize> {
    type Item<U: ?Sized, const M: usize>;
}

struct Maker;

struct Packet<T: ?Sized, U: ?Sized, const N: usize, const M: usize> {
    left: [u8; N],
    right: [u8; M],
    marker: PhantomData<(*const T, *const U)>,
}

impl<T: ?Sized, const N: usize> Family<T, N> for Maker {
    type Item<U: ?Sized, const M: usize> = Packet<T, U, N, M>;
}

impl<T: ?Sized, U: ?Sized, const N: usize, const M: usize> Packet<T, U, N, M> {
    fn build(_first: &T, _second: &U, left: [u8; N], right: [u8; M]) -> Self {
        Self { left, right, marker: PhantomData }
    }
}

fn main() {
    let packet = <Maker as Family<_, _>>::Item::<_, _>::build(
        &7u8,
        &19u16,
        [23, 29],
        [31, 37, 41],
    );
    assert_eq!(packet.left, [23, 29]);
    assert_eq!(packet.right, [31, 37, 41]);
}
