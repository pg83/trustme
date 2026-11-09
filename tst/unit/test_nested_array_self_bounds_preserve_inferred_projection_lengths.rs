trait Permit {}

impl<const N: usize> Permit for [u8; N] {}

trait Family<const N: usize> {
    type Output;
}

struct Maker;

struct Envelope<const N: usize>([u8; N]);

impl<const N: usize> Family<N> for Maker
where
    [u8; N]: Permit,
{
    type Output = Envelope<N>;
}

impl<const N: usize> Envelope<N> {
    fn build(bytes: [u8; N]) -> Self {
        Self(bytes)
    }
}

fn main() {
    let value = <Maker as Family<_>>::Output::build([2, 3, 5, 7]);
    assert_eq!(value.0, [2, 3, 5, 7]);
}
