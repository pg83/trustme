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

trait Pick: Sized {}
impl Pick for u8 {}

fn source<T: 'static>() -> &'static Wrap<T> {
    loop {}
}

fn opaque() -> &'static impl Pick {
    source()
}

fn main() {}
