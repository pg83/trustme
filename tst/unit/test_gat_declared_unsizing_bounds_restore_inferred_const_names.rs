#![feature(unsize)]

trait Build<const N: usize> {
    fn build(bytes: [u8; N]) -> Self;
}

trait Family {
    type Item<const N: usize>: Build<N>
    where
        [u8; N]: std::marker::Unsize<[u8]>;
}

struct Maker;

struct Buffer<const N: usize>([u8; N]);

impl Family for Maker {
    type Item<const N: usize> = Buffer<N>
    where
        [u8; N]: std::marker::Unsize<[u8]>;
}

impl<const N: usize> Build<N> for Buffer<N> {
    fn build(bytes: [u8; N]) -> Self {
        Self(bytes)
    }
}

fn through<F: Family, const N: usize>(bytes: [u8; N]) -> F::Item<N> {
    F::Item::<_>::build(bytes)
}

fn main() {
    let value = through::<Maker, 3>([79, 83, 89]);
    assert_eq!(value.0, [79, 83, 89]);
}
