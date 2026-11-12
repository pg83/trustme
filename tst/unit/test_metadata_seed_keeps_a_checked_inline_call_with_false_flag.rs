//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only -O

#![no_std]
#![crate_name = "own_checked_inline_seed"]

#[inline(never)]
pub fn may_fail<T>(value: &T, count: &mut usize, fail: bool) {
    if fail {
        panic!("checked inline branch");
    }
    *count = (*count).wrapping_add(core::mem::size_of_val(value));
}

#[inline(always)]
pub fn stable<T>(value: &T, count: &mut usize) {
    may_fail(value, count, false);
}

#[inline(always)]
pub fn entry<T>(value: &T, count: &mut usize) {
    stable(value, count);
}
