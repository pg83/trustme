//@ compile-variant: -O
#![feature(specialization, type_alias_impl_trait)]
#![allow(incomplete_features)]

use std::marker::PhantomData;
type First = impl Copy;
type Second = impl Copy;

struct Wrapped<T>(PhantomData<T>);
struct Flag<T>(PhantomData<T>);
trait Rank<Tag> { fn code() -> u8; }

impl<T> Rank<Flag<T>> for Wrapped<First> {
    default fn code() -> u8 { 13 }
}
impl Rank<Flag<u8>> for Wrapped<First> {
    fn code() -> u8 { 17 }
}
impl Rank<u16> for Wrapped<Second> {
    fn code() -> u8 { 19 }
}

fn a_check() {
    assert_eq!(<Wrapped<First> as Rank<Flag<u8>>>::code(), 17);
    assert_eq!(<Wrapped<First> as Rank<Flag<u32>>>::code(), 13);
    assert_eq!(<Wrapped<Second> as Rank<u16>>::code(), 19);
}

#[define_opaque(First, Second)]
fn z_values() -> (First, Second) { (23u8, 29u16) }

fn main() {
    a_check();
    let _ = z_values();
}
