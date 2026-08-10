// Extracted from library/core/src/slice/mod.rs:2121
#![allow(unused)]
fn main() {
    let v = [1, -2, 3, -4, 5, -6];

    {
       let (left, right) = v.split_at_checked(0).unwrap();
       assert_eq!(left, []);
       assert_eq!(right, [1, -2, 3, -4, 5, -6]);
    }

    {
        let (left, right) = v.split_at_checked(2).unwrap();
        assert_eq!(left, [1, -2]);
        assert_eq!(right, [3, -4, 5, -6]);
    }

    {
        let (left, right) = v.split_at_checked(6).unwrap();
        assert_eq!(left, [1, -2, 3, -4, 5, -6]);
        assert_eq!(right, []);
    }

    assert_eq!(None, v.split_at_checked(7));
}
