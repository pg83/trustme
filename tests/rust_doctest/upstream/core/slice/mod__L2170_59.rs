// Extracted from library/core/src/slice/mod.rs:2170
#![allow(unused)]
fn main() {
    let mut v = [1, 0, 3, 0, 5, 6];
    
    if let Some((left, right)) = v.split_at_mut_checked(2) {
        assert_eq!(left, [1, 0]);
        assert_eq!(right, [3, 0, 5, 6]);
        left[1] = 2;
        right[1] = 4;
    }
    assert_eq!(v, [1, 2, 3, 4, 5, 6]);
    
    assert_eq!(None, v.split_at_mut_checked(7));
}
