// Extracted from library/core/src/cell.rs:2015
#![allow(unused)]
fn main() {
    use std::cell::UnsafeCell;
    // Safety: the caller must ensure that there are no references that
    // point to the *contents* of the `UnsafeCell`.
    unsafe fn get_mut<T>(ptr: &UnsafeCell<T>) -> &mut T {
      unsafe { &mut *ptr.get() }
    }
}
