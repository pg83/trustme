// Extracted from library/core/src/slice/mod.rs:707
#![allow(unused)]
fn main() {
    let x = &[1, 2, 4];
    let x_ptr = x.as_ptr();

    unsafe {
        for i in 0..x.len() {
            assert_eq!(x.get_unchecked(i), &*x_ptr.add(i));
        }
    }
}
