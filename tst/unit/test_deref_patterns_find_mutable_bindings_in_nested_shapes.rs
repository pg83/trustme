#![feature(deref_patterns, deref_pure_trait)]
#![allow(incomplete_features)]
use std::ops::{Deref, DerefMut, DerefPure};
struct View<T> { inner: T }
impl<T> Deref for View<T> { type Target = T; fn deref(&self) -> &T { &self.inner } }
impl<T> DerefMut for View<T> { fn deref_mut(&mut self) -> &mut T { &mut self.inner } }
unsafe impl<T> DerefPure for View<T> {}
struct Record { left: u16, right: u16 }
enum Choice { Left { slot: u16 }, Right { slot: u16 } }
fn main() {
    let mut pair = View { inner: (3u16, 8u16) };
    let deref!((first, ref mut last)) = pair;
    *last += first;
    let deref!((first, last)) = pair;
    assert_eq!((first, last), (3, 11));
    let mut record = View { inner: Record { left: 5, right: 9 } };
    let deref!(Record { left, right: ref mut right }) = record;
    *right += left;
    let deref!(Record { left, right }) = record;
    assert_eq!((left, right), (5, 14));
    let mut array = View { inner: [7u16, 11u16] };
    let deref!([first, ref mut last]) = array;
    *last += first;
    let deref!([first, last]) = array;
    assert_eq!((first, last), (7, 18));
    let mut choice = View { inner: Choice::Right { slot: 19 } };
    let deref!((Choice::Left { slot: ref mut value } | Choice::Right { slot: ref mut value })) = choice;
    *value += 4;
    assert!(matches!(choice, deref!((Choice::Left { slot: 23 } | Choice::Right { slot: 23 }))));
    let choice = View { inner: Choice::Left { slot: 0 } };
    assert!(matches!(choice, deref!((Choice::Left { slot: 0 } | Choice::Right { slot: 0 }))));
}
