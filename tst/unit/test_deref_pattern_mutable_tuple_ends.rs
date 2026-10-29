#![feature(deref_patterns, deref_pure_trait)]
#![allow(incomplete_features)]
use std::ops::{Deref, DerefMut, DerefPure};
struct Wrapper<T>(T);
impl<T> Deref for Wrapper<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
impl<T> DerefMut for Wrapper<T> { fn deref_mut(&mut self) -> &mut T { &mut self.0 } }
unsafe impl<T> DerefPure for Wrapper<T> {}
struct Triple(u16, u16, u16);
fn main() {
    let mut tuple = Wrapper((2u16, 4u16, 7u16));
    let deref!((ref mut first, .., last)) = tuple;
    *first += last;
    let deref!((first, .., ref mut last)) = tuple;
    *last += first;
    let deref!((first, .., last)) = tuple;
    assert_eq!((first, last), (9, 16));

    let mut record = Wrapper(Triple(3, 5, 8));
    let deref!(Triple(ref mut first, .., last)) = record;
    *first += last;
    let deref!(Triple(first, .., ref mut last)) = record;
    *last += first;
    let deref!(Triple(first, .., last)) = record;
    assert_eq!((first, last), (11, 19));
}
