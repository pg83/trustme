//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only -O

#![no_std]
#![crate_name = "own_unwind_term_seed"]

#[inline(never)]
pub fn pulse<T>(_: &T, count: &mut usize) {
    *count += core::mem::size_of::<T>();
}

#[inline(always)]
pub extern "C" fn guarded<T>(value: &T, count: &mut usize, first: bool) {
    if first {
        pulse(value, count);
    } else {
        pulse(value, count);
    }
}
