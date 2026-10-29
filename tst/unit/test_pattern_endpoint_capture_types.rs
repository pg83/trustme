#![feature(pattern_types, pattern_type_macro, generic_const_exprs, generic_pattern_types)]
#![allow(incomplete_features, internal_features)]
use std::pat::pattern_type;
use std::marker::PhantomData;
struct Limits<T>(PhantomData<T>);
impl<T> Limits<T> {
    const LOW: usize = std::mem::size_of::<T>() + 1;
    const HIGH: usize = std::mem::size_of::<T>() + 4;
    const NEXT: usize = std::mem::size_of::<T>() + 32;
    const LAST: usize = std::mem::size_of::<T>() + 64;
}
type Window<T> = pattern_type!(usize is Limits::<T>::LOW..=Limits::<T>::HIGH | Limits::<T>::NEXT..=Limits::<T>::LAST);
fn window<T>(_: &T) -> PhantomData<Window<T>> { PhantomData }
fn main() {
    let input = [3u8; 4];
    let closure = move || input[2];
    let value = window(&closure);
    assert_eq!(std::mem::size_of_val(&value), 0);
    assert_eq!(closure(), 3);
}
