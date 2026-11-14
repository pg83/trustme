#![feature(coerce_unsized, unsize)]

use std::marker::Unsize;
use std::ops::CoerceUnsized;

trait Tag {
    type Value;
}

struct ByteTag;

impl Tag for ByteTag {
    type Value = u8;
}

struct View<T: ?Sized, M: Tag> {
    pointer: *const T,
    tag: M::Value,
}

impl<T: ?Sized + Unsize<U>, U: ?Sized, M: Tag> CoerceUnsized<View<U, M>> for View<T, M> {}

fn widen<T: ?Sized + Unsize<U>, U: ?Sized, M: Tag>(view: View<T, M>) -> View<U, M> {
    view
}

fn main() {
    let data = [13u16, 21, 34];
    let view = View::<[u16; 3], ByteTag> { pointer: &data, tag: 55 };
    let wide: View<[u16], ByteTag> = widen(view);
    assert_eq!(wide.tag, 55);
    assert_eq!(unsafe { &*wide.pointer }, &data);
}
