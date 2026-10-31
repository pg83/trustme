#![feature(deref_patterns, deref_pure_trait)]
#![allow(incomplete_features)]
use std::ops::{Deref, DerefMut, DerefPure};
struct Wrapped<T>(T);
impl<T> Deref for Wrapped<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
impl<T> DerefMut for Wrapped<T> { fn deref_mut(&mut self) -> &mut T { &mut self.0 } }
unsafe impl<T> DerefPure for Wrapped<T> {}
fn main() {
    let mut value = Wrapped((17u8, 23u8));
    let deref!((ref mut head, _)) = value;
    *head += 2;
    let selected = match value { deref!((19, tail)) => tail, _ => 0 };
    assert_eq!(selected, 23);
    assert_eq!(value.0, (19, 23));
    let mut boxed = Box::new((37u8, 41u8));
    let deref!((_, ref mut tail)) = boxed;
    *tail += 2;
    assert_eq!(*boxed, (37, 43));
}
