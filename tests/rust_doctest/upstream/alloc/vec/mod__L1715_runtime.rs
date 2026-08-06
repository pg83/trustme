// Extracted from library/alloc/src/vec/mod.rs:1715
#![allow(unused)]
extern crate alloc;
fn main() {
    unsafe {
        let mut v = vec![0, 1, 2];
        let ptr1 = v.as_ptr();
        let _ = ptr1.read();
        let ptr2 = v.as_mut_ptr().offset(2);
        ptr2.write(2);
        // Notably, the write to `ptr2` did *not* invalidate `ptr1`
        // because it mutated a different element:
        let _ = ptr1.read();
    }
}
