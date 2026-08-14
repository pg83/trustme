#![feature(core_intrinsics)]
#![allow(internal_features)]

const VALUE: u32 = core::intrinsics::black_box(42);

struct NoCopy;

const FORGOTTEN: () = {
    core::mem::forget(NoCopy);
};

fn main() {
    assert_eq!(VALUE, 42);
    assert_eq!(FORGOTTEN, ());
}
