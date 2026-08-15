#![feature(specialization)]

trait Select {
    type Output: Value;
}

struct Wrapper<T>(T);

impl<T> Select for Wrapper<T> {
    default type Output = u32;
}

trait Value {
    const VALUE: usize;
}

impl Value for u32 {
    const VALUE: usize = 0;
}

fn value() -> [u8; 0] {
    [0; <<Wrapper<u8> as Select>::Output as Value>::VALUE]
}

fn main() {}
