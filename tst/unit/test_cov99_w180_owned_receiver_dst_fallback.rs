//@ compile-only
//@ compile-flags: -Z dump-hir -Z stop-after=typeck
#![feature(auto_traits, negative_impls)]

use std::ops::Deref;

auto trait Direct {}
impl<T: ?Sized> !Direct for &T {}
impl<T: ?Sized> !Direct for &mut T {}

trait Select {
    fn select(self) -> u8;
}
impl<T: Direct> Select for T {
    fn select(self) -> u8 { 19 }
}

struct Tail<T: ?Sized>(T);
impl Deref for Tail<[u8]> {
    type Target = u8;
    fn deref(&self) -> &u8 { &self.0[0] }
}

fn main() {
    let storage = Tail([7u8]);
    let tail: &Tail<[u8]> = &storage;
    let selected: u8 = tail.select();
    assert_eq!(selected, 19);
}
