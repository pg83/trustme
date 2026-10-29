#![feature(pattern_types, pattern_type_macro, generic_const_exprs, generic_pattern_types)]
#![allow(incomplete_features, internal_features)]
use std::pat::pattern_type;
use std::marker::PhantomData;
struct Limits<T>(PhantomData<T>);
impl<T> Limits<T> {
    const LOW: isize = isize::MIN + (std::mem::size_of::<T>() as isize * 0);
    const HIGH: isize = -(std::mem::size_of::<T>() as isize);
    const NEXT: isize = std::mem::size_of::<T>() as isize;
    const LAST: isize = isize::MAX - (std::mem::size_of::<T>() as isize * 0);
}
type Window<T> = pattern_type!(isize is Limits::<T>::LOW..=Limits::<T>::HIGH | Limits::<T>::NEXT..=Limits::<T>::LAST);
type Below<T> = pattern_type!(isize is ..=Limits::<T>::HIGH);
trait HasWindow { type Window; type Below; }
struct Item([u8; 4]);
impl HasWindow for Item { type Window = Window<Self>; type Below = Below<Self>; }
impl HasWindow for [u8; 8] { type Window = Window<Self>; type Below = Below<Self>; }
fn main() {
    let value: <Item as HasWindow>::Window = unsafe { std::mem::transmute(6isize) };
    let raw: isize = unsafe { std::mem::transmute(value) };
    assert_eq!(raw, 6);
    let other: <[u8; 8] as HasWindow>::Window = unsafe { std::mem::transmute(-12isize) };
    let raw: isize = unsafe { std::mem::transmute(other) };
    assert_eq!(raw, -12);
    let below: <Item as HasWindow>::Below = unsafe { std::mem::transmute(-6isize) };
    let raw: isize = unsafe { std::mem::transmute(below) };
    assert_eq!(raw, -6);
    assert_eq!(Item([2; 4]).0[3], 2);
}
