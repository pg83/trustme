#![feature(unsize)]
use std::marker::Unsize;
struct Tagged<const N: usize, T: ?Sized> { header: [u8; N], tail: T }
fn widen<T: ?Sized + Unsize<U>, U: ?Sized>(value: &T) -> &U { value }
fn main() {
    let source = Tagged { header: [3, 7], tail: [11u16, 13, 17] };
    let target: &Tagged<_, [u16]> = widen(&source);
    assert_eq!(target.header, [3, 7]);
    assert_eq!(target.tail, [11, 13, 17]);
    let explicit: &Tagged<2, [u16]> = widen(&source);
    assert_eq!(explicit.tail.len(), 3);
}
