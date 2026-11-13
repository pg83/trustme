trait Leaf<const N: usize, const M: usize> {
    type Pair;
}

impl<const N: usize> Leaf<N, N> for () {
    type Pair = ([u8; N], [u16; N]);
}

trait Family<const N: usize, const M: usize> {
    type Output;
}

struct Maker;
struct Packet<const N: usize, const M: usize>([u8; N], [u16; M]);

impl<const N: usize, const M: usize> Family<N, M> for Maker
where
    (): Leaf<N, M, Pair = ([u8; N], [u16; M])>,
{
    type Output = Packet<N, M>;
}

impl<const N: usize, const M: usize> Packet<N, M> {
    fn build(bytes: [u8; N], words: [u16; M]) -> Self {
        Self(bytes, words)
    }
}

fn main() {
    let value = <Maker as Family<_, _>>::Output::build([41, 43], [269, 271]);
    assert_eq!(value.0, [41, 43]);
    assert_eq!(value.1, [269, 271]);
}
