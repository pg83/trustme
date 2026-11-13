//@ compile-fail: error:0:
#![feature(coerce_unsized, unsize)]

use std::marker::Unsize;
use std::ops::{CoerceUnsized, Deref};

struct View<T: ?Sized>(*const T);
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<View<U>> for View<T> {}

impl Deref for View<u8> {
    type Target = u32;
    fn deref(&self) -> &u32 { &7 }
}
impl Deref for View<u16> {
    type Target = u32;
    fn deref(&self) -> &u32 { &9 }
}

fn main() {
    let value = if true {
        View(0usize as *const _)
    } else {
        View(0usize as *const _)
    };
    let _: &u32 = &value;
}
