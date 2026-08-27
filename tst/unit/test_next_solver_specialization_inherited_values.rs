#![feature(specialization)]
#![allow(incomplete_features)]

// A specialising impl inherits value items from the nearest shadowed impl.
// Static getValue must ask the solver for the provider instead of repeating
// the legacy impl walk for methods and associated constants.
trait Items {
    const VALUE: usize;
    fn inherited() -> usize;
    fn overridden() -> usize;
}

struct Wrapper<T>(T);

impl<T> Items for Wrapper<T> {
    default const VALUE: usize = 1;

    default fn inherited() -> usize {
        Self::VALUE
    }

    default fn overridden() -> usize {
        0
    }
}

impl Items for Wrapper<u8> {
    fn overridden() -> usize {
        2
    }
}

fn main() {
    assert_eq!(<Wrapper<u8> as Items>::VALUE, 1);
    assert_eq!(<Wrapper<u8> as Items>::inherited(), 1);
    assert_eq!(<Wrapper<u8> as Items>::overridden(), 2);
}
