//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(never_type)]

fn never_reference() -> &'static ! {
    loop {}
}

fn accept<T: 'static, F: Fn() -> &'static T>(_: F) {}

pub fn infer_never_reference() {
    accept(|| never_reference());
}
