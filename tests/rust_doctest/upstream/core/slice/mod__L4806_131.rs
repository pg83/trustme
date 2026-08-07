// Extracted from library/core/src/slice/mod.rs:4806
#![allow(unused)]
#![feature(substr_range)]
fn main() {

    let arr: &[[u32; 2]] = &[[0, 1], [2, 3]];
    let flat_arr: &[u32] = arr.as_flattened();

    let ok_elm: &[u32; 2] = flat_arr[0..2].try_into().unwrap();
    let weird_elm: &[u32; 2] = flat_arr[1..3].try_into().unwrap();

    assert_eq!(ok_elm, &[0, 1]);
    assert_eq!(weird_elm, &[1, 2]);

    assert_eq!(arr.element_offset(ok_elm), Some(0)); // Points to element 0
    assert_eq!(arr.element_offset(weird_elm), None); // Points between element 0 and 1
}
