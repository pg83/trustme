//@ check-pass
//@ compile-flags: -Znext-solver

struct Lanes<T, const N: usize>([T; N]);

impl<T, const N: usize> From<[T; N]> for Lanes<T, N> {
    fn from(value: [T; N]) -> Self {
        Self(value)
    }
}

fn make<T, const N: usize>(value: [T; N]) -> Lanes<T, N> {
    Lanes::from(value)
}

fn main() {
    let _: Lanes<u8, 4> = make([0; 4]);
}
