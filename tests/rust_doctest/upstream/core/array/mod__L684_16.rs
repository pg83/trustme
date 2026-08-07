// Extracted from library/core/src/array/mod.rs:684
#![allow(unused)]
#![feature(split_array)]
fn main() {

    let v = [1, 2, 3, 4, 5, 6];

    {
       let (left, right) = v.split_array_ref::<0>();
       assert_eq!(left, &[]);
       assert_eq!(right, &[1, 2, 3, 4, 5, 6]);
    }

    {
        let (left, right) = v.split_array_ref::<2>();
        assert_eq!(left, &[1, 2]);
        assert_eq!(right, &[3, 4, 5, 6]);
    }

    {
        let (left, right) = v.split_array_ref::<6>();
        assert_eq!(left, &[1, 2, 3, 4, 5, 6]);
        assert_eq!(right, &[]);
    }
}
