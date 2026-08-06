// Extracted from src/const_eval.md:202
#![allow(unused)]
fn main() {
    use core::cell::UnsafeCell;
      const _: u8 = unsafe {
          let x: *mut u8 = &raw mut *&mut 0;
          //                        ^^^^^^^
          //             Dereference of mutable reference.
          *x = 1; // Dereference of mutable pointer.
          *(x as *const u8) // Dereference of constant pointer.
      };
      const _: u8 = unsafe {
          let x = &UnsafeCell::new(0);
          *x.get() = 1; // Mutation of interior mutable value.
          *x.get()
      };
}
