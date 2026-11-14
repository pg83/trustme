//@ crate-type: lib

struct Pair<A, B>(A, B);

trait Repeated {
    fn value() -> u8;
}

impl<T> Repeated for Pair<T, T> {
    fn value() -> u8 { 7 }
}

pub fn probe() -> u8 {
    Pair::<_, u8>::value()
}
