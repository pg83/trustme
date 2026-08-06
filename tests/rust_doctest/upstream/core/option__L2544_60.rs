// Extracted from library/core/src/option.rs:2544
#![allow(unused)]
fn main() {
    let items = vec![0_u16, 1, 2];
    
    let res: Option<Vec<u16>> = items
        .iter()
        .map(|x| x.checked_add(1))
        .collect();
    
    assert_eq!(res, Some(vec![1, 2, 3]));
}
