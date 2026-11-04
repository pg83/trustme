//@ compile-fail: error:
#![feature(arbitrary_self_types)]

use std::marker::PhantomData;
use std::ops::Deref;

type Byte = u8;

struct Wrapped<T, U: ?Sized>(T, PhantomData<U>);

impl<T, U: ?Sized> Deref for Wrapped<T, U> {
    type Target = T;
    fn deref(&self) -> &T { &self.0 }
}

type Receiver<T> = Wrapped<T, dyn std::fmt::Debug + Send<Byte> + Sync<Byte>>;

struct Number;

impl Number {
    fn invalid(self: Receiver<Self>) {}
}

fn main() {}
