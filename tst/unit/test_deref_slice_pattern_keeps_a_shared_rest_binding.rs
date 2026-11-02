//@ compile-variant: -O
#![feature(deref_patterns, deref_pure_trait)]
#![allow(incomplete_features)]
use std::ops::{Deref, DerefMut, DerefPure};
struct Lens<T>(T);
impl<T> Deref for Lens<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
impl<T> DerefMut for Lens<T> { fn deref_mut(&mut self) -> &mut T { &mut self.0 } }
unsafe impl<T> DerefPure for Lens<T> {}
fn main() {
    let value = Lens([3u32, 5, 7, 11]);
    let deref!([first, ref middle @ .., last]) = value;
    assert_eq!((first, last), (3, 11));
    assert_eq!(*middle, [5, 7]);
}
