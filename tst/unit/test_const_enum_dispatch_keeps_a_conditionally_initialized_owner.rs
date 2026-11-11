//@ compile-flags: -Zmir-opt-level=3 -Zdump-mir
#![feature(adt_const_params)]
#![allow(incomplete_features)]

use std::cell::Cell;
use std::marker::ConstParamTy;

#[derive(Clone, Copy, PartialEq, Eq, ConstParamTy)]
enum Destination {
    Discard(u8),
    Store(u8),
}

struct Owner<'a>(&'a Cell<u32>);

impl Drop for Owner<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

fn assign<T>(destination: Destination, value: T) {
    let _stored: T;
    match destination {
        Destination::Discard(_) => std::mem::forget(value),
        Destination::Store(_) => _stored = value,
    }
}

fn dispatch<const D: Destination>(value: Owner<'_>) {
    assign(D, value);
}

fn main() {
    let releases = Cell::new(0);
    dispatch::<{ Destination::Discard(7) }>(Owner(&releases));
    assert_eq!(releases.get(), 0);
    dispatch::<{ Destination::Store(11) }>(Owner(&releases));
    assert_eq!(releases.get(), 1);
}
