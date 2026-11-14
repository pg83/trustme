//@ compile-fail: error:
#![feature(specialization)]

trait Inner {
    type A;
    type B;
}

impl<T> Inner for T {
    default type A = u16;
    default type B = u16;
}

struct Chosen;

impl Inner for Chosen {
    default type A = u8;
    default type B = u8;
}

trait Select {}

impl<T, V> Select for (T, V)
where
    T: Inner<A = u16, B = u16>,
{}

fn require<S: Select>() {}

fn main() {
    require::<(Chosen, _)>();
}
