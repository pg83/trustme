//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

trait Project {
    type Item;
}

impl Project for u8 {
    type Item = u16;
}

struct Carrier<T>(T);

impl<T, U> Carrier<T>
where
    T: Project<Item = U>,
{
    type Output = U;
}

trait Check<T> {}
impl Check<(u16, u8)> for () {}
impl Check<(u16, i32)> for () {}

struct Receiver;

impl Receiver {
    fn probe<T>(&self, _: T)
    where
        (): Check<(Carrier<u8>::Output, T)>,
    {
    }
}

pub fn exercise() {
    Receiver.probe(0);
}
