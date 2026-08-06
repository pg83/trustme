// Extracted from library/core/src/slice/mod.rs:5010
#![allow(unused)]
fn main() {
    fn add_5_to_all(slice: &mut [i32]) {
        for i in slice {
            *i += 5;
        }
    }
    
    let mut array = [[1, 2, 3], [4, 5, 6], [7, 8, 9]];
    add_5_to_all(array.as_flattened_mut());
    assert_eq!(array, [[6, 7, 8], [9, 10, 11], [12, 13, 14]]);
}
