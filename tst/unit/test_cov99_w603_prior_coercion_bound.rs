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

fn require<T: Pick>(_: &T) {}

fn unknown<T>() -> T {
    loop {}
}

fn main() {
    let source = Wrap(unknown());
    let source_ref = &source;
    let selected: &_ = source_ref;
    require(selected);
}
