//@ compile-flags: -Zmir-opt-level=3 -Zdump-mir
#![feature(adt_const_params)]
#![allow(incomplete_features)]

use std::cell::Cell;
use std::marker::ConstParamTy;

#[derive(Clone, Copy, PartialEq, Eq, ConstParamTy)]
enum Allocation {
    Empty(u8),
    Create(u8),
}

struct Resource<'a>(&'a Cell<u32>);

impl Drop for Resource<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

fn allocate(allocation: Allocation, releases: &Cell<u32>) {
    let _resource: Resource<'_>;
    match allocation {
        Allocation::Empty(_) => (),
        Allocation::Create(_) => _resource = Resource(releases),
    }
}

fn dispatch<const A: Allocation>(releases: &Cell<u32>) {
    allocate(A, releases);
}

fn main() {
    let releases = Cell::new(0);
    dispatch::<{ Allocation::Empty(13) }>(&releases);
    assert_eq!(releases.get(), 0);
    dispatch::<{ Allocation::Create(17) }>(&releases);
    assert_eq!(releases.get(), 1);
}
