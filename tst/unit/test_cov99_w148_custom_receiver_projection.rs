//@ crate-type: lib
#![feature(arbitrary_self_types)]

use std::marker::PhantomData;
use std::ops::Deref;

struct Handle<T, U>(T, PhantomData<U>);

impl<T, U> Deref for Handle<T, U> {
    type Target = T;
    fn deref(&self) -> &T { &self.0 }
}

trait Family { type Item; }
impl Family for () { type Item = u16; }
impl Family for bool { type Item = u32; }

trait Choose: Sized {
    fn choose<A: Family>(self: Handle<Self, A::Item>, _: A) {}
}

impl Choose for u8 {}

pub fn exercise() {
    let receiver = Handle::<u8, u16>(7, PhantomData);
    receiver.choose(());
}
