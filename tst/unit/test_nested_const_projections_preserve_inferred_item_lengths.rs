trait Width<const N: usize> {
    type Output;
}

trait Family<const N: usize> {
    type Item;
}

struct Maker;
struct Packet<const N: usize>([u8; N]);

impl<const N: usize> Width<N> for () {
    type Output = Packet<N>;
}

impl<const N: usize> Family<N> for Maker {
    type Item = <() as Width<N>>::Output;
}

impl<const N: usize> Packet<N> {
    fn build(bytes: [u8; N]) -> Self { Self(bytes) }
}

fn main() {
    let first = <Maker as Family<_>>::Item::build([31, 37]);
    let second = <Maker as Family<_>>::Item::build([41, 43, 47, 53]);
    assert_eq!(first.0, [31, 37]);
    assert_eq!(second.0, [41, 43, 47, 53]);
}
