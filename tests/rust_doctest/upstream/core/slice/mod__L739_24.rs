// Extracted from library/core/src/slice/mod.rs:739
#![allow(unused)]
fn main() {
    let x = &mut [1, 2, 4];
    let x_ptr = x.as_mut_ptr();

    unsafe {
        for i in 0..x.len() {
            *x_ptr.add(i) += 2;
        }
    }
    assert_eq!(x, &[3, 4, 6]);
}
