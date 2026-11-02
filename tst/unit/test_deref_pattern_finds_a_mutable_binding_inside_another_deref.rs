//@ compile-variant: -O
#![feature(deref_patterns, deref_pure_trait, box_patterns)]
#![allow(incomplete_features)]
use std::ops::{Deref, DerefMut, DerefPure};
struct Lens<T>(T);
impl<T> Deref for Lens<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
impl<T> DerefMut for Lens<T> { fn deref_mut(&mut self) -> &mut T { &mut self.0 } }
unsafe impl<T> DerefPure for Lens<T> {}
fn main() {
    let mut value = Lens(Lens((7u32, 11u32)));
    { let deref!(deref!((ref mut first, last))) = value; *first += last; }
    assert_eq!(value.0.0, (18, 11));
}
