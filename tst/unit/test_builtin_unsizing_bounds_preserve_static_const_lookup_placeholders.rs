#![feature(unsize)]

trait Factory<const N: usize> {
    type Output;
}

struct Maker;

struct Buffer<const N: usize>([u8; N]);

impl<const N: usize> Factory<N> for Maker
where
    [u8; N]: std::marker::Unsize<[u8]>,
{
    type Output = Buffer<N>;
}

impl<const N: usize> Buffer<N> {
    fn build(bytes: [u8; N]) -> Self {
        Self(bytes)
    }

    fn slice(&self) -> &[u8] {
        &self.0
    }
}

fn main() {
    let value = <Maker as Factory<_>>::Output::build([67, 71, 73]);
    assert_eq!(value.slice(), &[67, 71, 73]);
}
