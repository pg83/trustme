// Extracted from library/core/src/slice/mod.rs:668
#![allow(unused)]
fn main() {
    let x = &mut [1, 2, 4];

    unsafe {
        let elem = x.get_unchecked_mut(1);
        *elem = 13;
    }
    assert_eq!(x, &[1, 13, 4]);
}
