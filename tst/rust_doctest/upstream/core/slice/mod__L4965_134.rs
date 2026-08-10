// Extracted from library/core/src/slice/mod.rs:4965
#![allow(unused)]
fn main() {
    assert_eq!([[1, 2, 3], [4, 5, 6]].as_flattened(), &[1, 2, 3, 4, 5, 6]);

    assert_eq!(
        [[1, 2, 3], [4, 5, 6]].as_flattened(),
        [[1, 2], [3, 4], [5, 6]].as_flattened(),
    );

    let slice_of_empty_arrays: &[[i32; 0]] = &[[], [], [], [], []];
    assert!(slice_of_empty_arrays.as_flattened().is_empty());

    let empty_slice_of_arrays: &[[u32; 10]] = &[];
    assert!(empty_slice_of_arrays.as_flattened().is_empty());
}
