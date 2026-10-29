#![feature(type_alias_impl_trait)]
use std::marker::PhantomData;
type Hidden<T: Copy> = impl Copy;
#[define_opaque(Hidden)]
fn hidden<T: Copy>(value: T) -> Hidden<T> { value }
struct Pair<T, U>(PhantomData<(T, U)>);
impl<T> Pair<T, T> {
    const CODE: u8 = 7;
    fn code() -> u8 { Self::CODE }
}
fn through<T: Copy>(_: Hidden<T>) -> u8 {
    Pair::<Hidden<T>, Hidden<T>>::code() + Pair::<Hidden<T>, Hidden<T>>::CODE
}
fn main() { assert_eq!(through::<u8>(hidden(3u8)), 14); }
