//@ compile-fail: Type mismatch
//@ compile-flags: --emit=metadata -Cpanic=abort

use std::ops::Neg;

trait Item {
    type Out;
}

struct Wrap<T: Item>(T::Out);

impl<T: Item> Neg for Wrap<T> {
    type Output = T::Out;

    fn neg(self) -> T::Out {
        self.0
    }
}

fn mismatch<T: Item, U: Item>(value: Wrap<T>) -> U::Out {
    -value
}

fn main() {}
