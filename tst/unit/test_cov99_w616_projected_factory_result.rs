//@ compile-fail: error:
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Pair {
    type Left;
    type Right;
}

struct Source<T>(T);

trait Factory<I> {
    fn produce(input: I) -> Self;
}

impl<T: Pair> Factory<Source<T>> for T::Left {
    fn produce(_: Source<T>) -> Self {
        loop {}
    }
}

fn reject<T: Pair>(value: T) {
    let made = <_ as Factory<Source<T>>>::produce(Source(value));
    let _: T::Right = made;
}

fn main() {}
