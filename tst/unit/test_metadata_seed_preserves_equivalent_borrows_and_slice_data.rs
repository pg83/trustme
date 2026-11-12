//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only -O

#![no_std]
#![crate_name = "own_equivalent_mir_seed"]

#[inline(never)]
pub fn observe<T>(_: &T, count: &mut usize) {
    *count += core::mem::size_of::<T>();
}

pub fn borrowed<T>(value: &T, count: &mut usize, flag: bool) {
    if flag {
        observe(value, count);
    } else {
        observe(value, count);
    }
}

pub fn data<T>(value: *const [T], flag: bool) -> *const T {
    if flag {
        value as *const T
    } else {
        value as *const T
    }
}
