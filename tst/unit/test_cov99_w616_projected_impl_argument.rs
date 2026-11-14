//@ compile-fail: error:
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Pair {
    type Left;
    type Right;
}

trait Accept<U> {
    fn accept(self, value: U);
}

impl<T: Pair> Accept<T::Left> for T {
    fn accept(self, _: T::Left) {}
}

fn reject<T: Pair>(receiver: T, value: T::Right) {
    receiver.accept(value);
}

fn main() {}
