#![feature(generic_const_exprs)]

trait Value {
    const VALUE: usize;
}

impl Value for u16 {
    const VALUE: usize = 13;
}

fn generic<T: Value, const N: usize>() -> [u8; N + T::VALUE] {
    [0; N + T::VALUE]
}

fn concrete<const N: usize>() -> [u8; N + 13] {
    generic::<u16, N>()
}

fn main() {}
