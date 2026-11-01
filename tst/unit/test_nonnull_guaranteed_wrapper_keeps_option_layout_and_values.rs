//@ compile-variant: -O
#![feature(rustc_attrs)]
#![allow(internal_features)]
use std::num::NonZeroU16;

#[repr(transparent)]
#[rustc_nonnull_optimization_guaranteed]
struct Key(NonZeroU16);

#[inline(never)]
fn make(value: u16) -> Option<Key> { NonZeroU16::new(value).map(Key) }
fn main() {
    assert_eq!(std::mem::size_of::<Option<Key>>(), 2);
    assert!(make(0).is_none());
    assert_eq!(make(7).unwrap().0.get(), 7);
    assert_eq!(make(65535).unwrap().0.get(), 65535);
}
