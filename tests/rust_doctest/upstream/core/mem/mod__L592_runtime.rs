// Extracted from library/core/src/mem/mod.rs:592
#![allow(unused)]
fn main() {
    use std::{mem, ptr};

    pub struct MyCollection<T> {
      data: [T; 1],
        /* ... */
    }
    impl<T> MyCollection<T> {
      fn iter_mut(&mut self) -> &mut [T] { &mut self.data }
      fn free_buffer(&mut self) {}
    }

    impl<T> Drop for MyCollection<T> {
        fn drop(&mut self) {
            unsafe {
                // drop the data
                if mem::needs_drop::<T>() {
                    for x in self.iter_mut() {
                        ptr::drop_in_place(x);
                    }
                }
                self.free_buffer();
            }
        }
    }
}
