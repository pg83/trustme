#![feature(unsize)]

trait Build<T, const N: usize> {
    fn build(value: T, bytes: [u8; N]) -> Self;
}

trait Family {
    type Item<const N: usize, T>: Build<T, N>
    where
        [u8; N]: std::marker::Unsize<[u8]>;
}

struct Maker;
struct Buffer<T, const N: usize>(T, [u8; N]);

impl Family for Maker {
    type Item<const N: usize, T> = Buffer<T, N>
    where
        [u8; N]: std::marker::Unsize<[u8]>;
}

impl<T, const N: usize> Build<T, N> for Buffer<T, N> {
    fn build(value: T, bytes: [u8; N]) -> Self {
        Self(value, bytes)
    }
}

fn through<F: Family, const N: usize>(bytes: [u8; N]) -> F::Item<N, u16> {
    F::Item::<_, u16>::build(97, bytes)
}

fn main() {
    let value = through::<Maker, 3>([101, 103, 107]);
    assert_eq!(value.0, 97u16);
    assert_eq!(value.1, [101, 103, 107]);
}
