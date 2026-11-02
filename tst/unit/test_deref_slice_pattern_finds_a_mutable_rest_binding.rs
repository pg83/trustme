//@ compile-variant: -O
#![feature(deref_patterns, deref_pure_trait, box_patterns)]
#![allow(incomplete_features)]
use std::ops::{Deref, DerefMut, DerefPure};
struct Lens<T>(T);
impl<T> Deref for Lens<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
impl<T> DerefMut for Lens<T> { fn deref_mut(&mut self) -> &mut T { &mut self.0 } }
unsafe impl<T> DerefPure for Lens<T> {}
fn main() {
    let mut value = Lens([2u32, 3, 5, 7]);
    { let deref!([first, ref mut middle @ .., last]) = value; middle[0] += first; middle[1] += last; }
    assert_eq!(value.0, [2, 5, 12, 7]);
}
