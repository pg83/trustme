#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

struct Wrapper<T>(T);

impl<T> Wrapper<T> {
    pub type Item = T;
}

fn identity(value: Wrapper<u32>::Item) -> Wrapper<u32>::Item {
    value
}

fn main() {
    let value: Wrapper<u32>::Item = identity(42);
    assert_eq!(value, 42);
}
