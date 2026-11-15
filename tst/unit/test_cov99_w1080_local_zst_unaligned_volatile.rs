//@ compile-flags: -Copt-level=0
#![feature(core_intrinsics)]
#![allow(internal_features)]

fn main() {
    let mut value = ();
    unsafe {
        core::intrinsics::unaligned_volatile_store(&mut value, ());
        let loaded = core::intrinsics::unaligned_volatile_load(&value);
        assert_eq!(loaded, ());
    }
}
