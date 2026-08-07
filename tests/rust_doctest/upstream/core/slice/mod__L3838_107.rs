// Extracted from library/core/src/slice/mod.rs:3838
#![allow(unused)]
fn main() {
    let mut slice = [1, 2, 3, 4, 5];

    {
        let (left, right) = slice.split_at_mut(2);
        left.copy_from_slice(&right[1..]);
    }

    assert_eq!(slice, [4, 5, 3, 4, 5]);
}
