#![feature(intrinsics)]

#[rustc_intrinsic]
unsafe fn vtable_size(_vtable: *const ()) -> usize {
    panic!("the intrinsic fallback body ran")
}

trait Marker {}

impl Marker for () {}

fn main() {
    let value: &dyn Marker = &();
    let (_, vtable): (*const (), *const ()) = unsafe { core::mem::transmute(value) };
    assert_eq!(unsafe { vtable_size(vtable) }, 0);
}
