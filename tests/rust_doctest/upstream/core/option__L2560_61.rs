// Extracted from library/core/src/option.rs:2560
#![allow(unused)]
fn main() {
    let items = vec![2_u16, 1, 0];
    
    let res: Option<Vec<u16>> = items
        .iter()
        .map(|x| x.checked_sub(1))
        .collect();
    
    assert_eq!(res, None);
}
