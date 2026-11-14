//@ compile-fail: :
//@ compile-flags: --emit=metadata -Cpanic=abort

use std::ops::Deref;

trait Payload {
    type Item;
}

impl Payload for () {
    type Item = u8;
}

struct Wrap<T>(T);

impl<T, U> Deref for Wrap<T>
where
    T: Payload<Item = U>,
{
    type Target = U;

    fn deref(&self) -> &U {
        loop {}
    }
}

trait Pick: Sized {
    fn pick() -> Self;
}

impl Pick for u8 {
    fn pick() -> Self {
        0
    }
}

fn unknown<T>() -> T {
    loop {}
}

fn main() {
    let source = Wrap(unknown());
    let target = Pick::pick();
    let mut reference = &target;
    reference = &source;
    let _ = reference;
}
