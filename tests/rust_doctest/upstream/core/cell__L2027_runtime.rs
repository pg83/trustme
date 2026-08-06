// Extracted from library/core/src/cell.rs:2027
#![allow(unused)]
fn main() {
    use std::cell::UnsafeCell;
    fn get_shared<T>(ptr: &mut T) -> &UnsafeCell<T> {
      let t = ptr as *mut T as *const UnsafeCell<T>;
      // SAFETY: `T` and `UnsafeCell<T>` have the same memory layout
      unsafe { &*t }
    }
}
