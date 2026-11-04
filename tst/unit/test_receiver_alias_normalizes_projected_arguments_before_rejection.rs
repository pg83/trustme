//@ compile-fail: Unknown receiver type
#![feature(inherent_associated_types)]

type Byte = u8;
type ReceiverFn<T> = fn() -> T;

struct Number;

impl Number {
    type Projection<T> = u32;

    fn invalid(self: ReceiverFn<Self::Projection<Byte>>) {}
}

fn main() {}
