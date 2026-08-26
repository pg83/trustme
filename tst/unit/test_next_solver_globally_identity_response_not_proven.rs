//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// An ambiguous-identity response (Copy on a union whose parameter is still
// an inference variable) is non-committal by definition and must never be
// promoted to a proven environment response constraint.  Mirrors
// std io/copy.rs stack_buffer_copy ([MaybeUninit::uninit(); N] repeat).

#![feature(core_io_borrowed_buf)]
use core::io::BorrowedBuf;
use core::mem::MaybeUninit;

pub fn f() -> usize {
    let buf: &mut [_] = &mut [MaybeUninit::uninit(); 64];
    let mut buf: BorrowedBuf<'_> = buf.into();
    buf.capacity()
}
