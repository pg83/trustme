//@ compile-variant: -O
#![feature(more_qualified_paths, generic_const_items)]
#![allow(incomplete_features)]
struct Record<T> { value: T }
struct Identity<T>(T);
trait Project { type Output; }
impl<T> Project for Identity<T> { type Output = Record<fn() -> usize>; }
fn width<T>() -> usize { std::mem::size_of::<T>() }
const SELECT<T>: fn() -> usize = {
    let <Identity<T> as Project>::Output { value } = Record { value: width::<T> as fn() -> usize };
    value
};
fn main() {
    assert_eq!(SELECT::<u16>(), 2);
    assert_eq!(SELECT::<u32>(), 4);
}
