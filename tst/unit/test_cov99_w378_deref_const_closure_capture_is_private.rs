//@ compile-fail: Couldn't find the field 0
//@ compile-flags: -Z dump-hir

#![feature(type_alias_impl_trait)]

use std::ops::Deref;

type Capture = impl Fn() -> usize;

#[define_opaque(Capture)]
const fn capture(value: usize) -> Capture {
    move || value
}

const SAVED: &Capture = &capture(23);

struct Handle;

impl Deref for Handle {
    type Target = Capture;

    fn deref(&self) -> &Capture {
        SAVED
    }
}

#[define_opaque(Capture)]
fn inspect_capture() {
    let _ = Handle.0;
}

fn main() {}
