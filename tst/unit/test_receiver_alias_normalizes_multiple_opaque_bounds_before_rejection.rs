//@ compile-fail: Unknown receiver type
#![feature(type_alias_impl_trait)]

type Byte = u8;

trait Left<T> {}
trait Right<T> {}
impl Left<u8> for u8 {}
impl Right<u8> for u8 {}

type Hidden = impl Left<Byte> + Right<Byte>;

#[define_opaque(Hidden)]
fn make_hidden() -> Hidden { 7u8 }

type ReceiverFn<T> = fn() -> T;

struct Number;

impl Number {
    fn invalid(self: ReceiverFn<Hidden>) {}
}

fn main() {}
