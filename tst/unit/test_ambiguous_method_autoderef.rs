//@ compile-fail: type annotations needed

use std::ops::Deref;

trait Probe {
    fn probe(&self) {}
}

struct First;

impl Probe for First {}

struct Second;

impl Probe for Second {}

struct Ambiguous<T>(T);

impl Deref for Ambiguous<u8> {
    type Target = First;

    fn deref(&self) -> &Self::Target {
        &First
    }
}

impl Deref for Ambiguous<u16> {
    type Target = Second;

    fn deref(&self) -> &Self::Target {
        &Second
    }
}

fn main() {
    let value = Ambiguous(Default::default());
    value.probe();
}
