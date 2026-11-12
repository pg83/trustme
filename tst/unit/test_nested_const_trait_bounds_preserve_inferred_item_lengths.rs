trait Stamp<const N: usize> {}
impl<const N: usize> Stamp<N> for () {}

trait Family<const N: usize> {
    type Item;
}

struct Maker;
struct Packet<const N: usize>([u8; N]);

impl<const N: usize> Family<N> for Maker
where (): Stamp<N>,
{
    type Item = Packet<N>;
}

impl<const N: usize> Packet<N> {
    fn build(bytes: [u8; N]) -> Self { Self(bytes) }
}

fn main() {
    let first = <Maker as Family<_>>::Item::build([13, 17, 19]);
    let second = <Maker as Family<_>>::Item::build([23, 29]);
    assert_eq!(first.0, [13, 17, 19]);
    assert_eq!(second.0, [23, 29]);
}
