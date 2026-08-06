// Extracted from library/core/src/primitive_docs.rs:1781
#![allow(unused)]
fn main() {
    #[cfg(not(miri))] { // FIXME: use strict provenance APIs once they are stable, then remove this `cfg`
    let fnptr: fn(i32) -> i32 = |x| x+2;
    let fnptr_addr = fnptr as usize;
    let fnptr = fnptr_addr as *const ();
    let fnptr: fn(i32) -> i32 = unsafe { std::mem::transmute(fnptr) };
    assert_eq!(fnptr(40), 42);
    }
}
