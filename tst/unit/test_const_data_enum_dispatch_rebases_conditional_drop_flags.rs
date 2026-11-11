//@ compile-flags: -Zmir-opt-level=3
#![feature(adt_const_params)]
#![allow(incomplete_features)]

use std::cell::Cell;
use std::marker::ConstParamTy;

#[derive(Clone, Copy, PartialEq, Eq, ConstParamTy)]
enum Decision {
    Forget(u8),
    Release(u8),
}

struct Resource<'a>(&'a Cell<u32>);

impl Drop for Resource<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

fn choose<T>(decision: Decision, value: T) {
    match decision {
        Decision::Forget(_) => std::mem::forget(value),
        Decision::Release(_) => (),
    }
}

fn dispatch<const D: Decision>(value: Resource<'_>) {
    choose(D, value);
}

fn main() {
    let releases = Cell::new(0);
    dispatch::<{ Decision::Forget(3) }>(Resource(&releases));
    assert_eq!(releases.get(), 0);
    dispatch::<{ Decision::Release(5) }>(Resource(&releases));
    assert_eq!(releases.get(), 1);
}
