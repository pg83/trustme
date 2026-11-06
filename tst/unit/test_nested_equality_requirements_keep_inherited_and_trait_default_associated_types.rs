#![feature(specialization, associated_type_defaults)]
#![allow(incomplete_features)]
trait Source { type Value; }
impl<T> Source for (T,) { default type Value = u16; }
impl Source for (u8,) {}
trait Accepted { fn width() -> usize; }
impl<T: Source<Value = u16>> Accepted for T {
    fn width() -> usize { std::mem::size_of::<T::Value>() }
}
fn width<T: Accepted>(_: T) -> usize { T::width() }
trait Defaults { type Value = u32; }
impl Defaults for bool {}
fn default_width<T: Defaults<Value = u32>>(_: T) -> usize {
    std::mem::size_of::<T::Value>()
}
fn main() {
    assert_eq!(width((17u8,)), 2);
    assert_eq!(default_width(true), 4);
}
