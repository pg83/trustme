// Extracted from library/core/src/slice/mod.rs:5207
#![allow(unused)]
fn main() {
    use std::slice::GetDisjointMutError;
    
    let v = &mut [1, 2, 3];
    assert_eq!(v.get_disjoint_mut([0, 999]), Err(GetDisjointMutError::IndexOutOfBounds));
    assert_eq!(v.get_disjoint_mut([1, 1]), Err(GetDisjointMutError::OverlappingIndices));
}
