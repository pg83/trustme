//@ compile-fail: Couldn't find the field 0
//@ compile-flags: -Z dump-hir

#![feature(type_alias_impl_trait)]

type Capture = impl Fn() -> usize;

#[define_opaque(Capture)]
const fn capture(value: usize) -> Capture {
    move || value
}

const SAVED: &Capture = &capture(23);

#[define_opaque(Capture)]
fn inspect_capture() {
    let _ = SAVED.0;
}

fn main() {}
