//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(const_heap, core_intrinsics)]

pub static ALIGNED_BYTE: &u8 = unsafe {
    let memory = core::intrinsics::const_allocate(1, 64);
    *memory = 37;
    &*core::intrinsics::const_make_global(memory)
};
