// Extracted from library/core/src/array/mod.rs:130
#![allow(unused)]
#![feature(array_try_from_fn)]
fn main() {
    
    let array: Result<[u8; 5], _> = std::array::try_from_fn(|i| i.try_into());
    assert_eq!(array, Ok([0, 1, 2, 3, 4]));
    
    let array: Result<[i8; 200], _> = std::array::try_from_fn(|i| i.try_into());
    assert!(array.is_err());
    
    let array: Option<[_; 4]> = std::array::try_from_fn(|i| i.checked_add(100));
    assert_eq!(array, Some([100, 101, 102, 103]));
    
    let array: Option<[_; 4]> = std::array::try_from_fn(|i| i.checked_sub(100));
    assert_eq!(array, None);
}
