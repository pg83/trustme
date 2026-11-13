trait Leaf<const N: usize> {
    type Pair;
}

impl<const N: usize> Leaf<N> for () {
    type Pair = ([u8; N], [u16; N]);
}

trait Relay<const N: usize> {
    type Pair;
}

impl<const N: usize> Relay<N> for ()
where
    (): Leaf<N, Pair = ([u8; N], [u16; N])>,
{
    type Pair = ([u8; N], [u16; N]);
}

trait Family<const N: usize> {
    type Output;
}

struct Maker;
struct Packet<const N: usize>([u8; N], [u16; N]);

impl<const N: usize> Family<N> for Maker
where
    (): Relay<N, Pair = ([u8; N], [u16; N])>,
{
    type Output = Packet<N>;
}

impl<const N: usize> Packet<N> {
    fn build(bytes: [u8; N], words: [u16; N]) -> Self {
        Self(bytes, words)
    }
}

fn main() {
    let value = <Maker as Family<_>>::Output::build([31, 37], [257, 263]);
    assert_eq!(value.0, [31, 37]);
    assert_eq!(value.1, [257, 263]);
}
