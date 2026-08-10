// Extracted from library/core/src/slice/mod.rs:625
#![allow(unused)]
fn main() {
    let x = &[1, 2, 4];

    unsafe {
        assert_eq!(x.get_unchecked(1), &2);
    }
}
