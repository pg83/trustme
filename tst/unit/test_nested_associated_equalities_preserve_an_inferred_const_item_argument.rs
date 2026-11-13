trait Leaf<const N: usize> {
    type Bytes;
}

impl<const N: usize> Leaf<N> for () {
    type Bytes = [u8; N];
}

trait Relay<const N: usize> {
    type Bytes;
}

impl<const N: usize> Relay<N> for ()
where
    (): Leaf<N, Bytes = [u8; N]>,
{
    type Bytes = [u8; N];
}

trait Family<const N: usize> {
    type Output;
}

struct Maker;
struct Packet<const N: usize>([u8; N]);

impl<const N: usize> Family<N> for Maker
where
    (): Relay<N, Bytes = [u8; N]>,
{
    type Output = Packet<N>;
}

impl<const N: usize> Packet<N> {
    fn build(bytes: [u8; N]) -> Self {
        Self(bytes)
    }
}

fn main() {
    let first = <Maker as Family<_>>::Output::build([13, 17, 19]);
    let second = <Maker as Family<_>>::Output::build([23, 29]);
    assert_eq!(first.0, [13, 17, 19]);
    assert_eq!(second.0, [23, 29]);
}
