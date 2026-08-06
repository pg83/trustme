// Extracted from library/core/src/array/mod.rs:72
#![allow(unused)]
fn main() {
    // type inference is helping us here, the way `from_fn` knows how many
    // elements to produce is the length of array down there: only arrays of
    // equal lengths can be compared, so the const generic parameter `N` is
    // inferred to be 5, thus creating array of 5 elements.
    
    let array = core::array::from_fn(|i| i);
    // indexes are:    0  1  2  3  4
    assert_eq!(array, [0, 1, 2, 3, 4]);
    
    let array2: [usize; 8] = core::array::from_fn(|i| i * 2);
    // indexes are:     0  1  2  3  4  5   6   7
    assert_eq!(array2, [0, 2, 4, 6, 8, 10, 12, 14]);
    
    let bool_arr = core::array::from_fn::<_, 5, _>(|i| i % 2 == 0);
    // indexes are:       0     1      2     3      4
    assert_eq!(bool_arr, [true, false, true, false, true]);
}
