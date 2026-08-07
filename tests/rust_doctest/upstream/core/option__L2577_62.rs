// Extracted from library/core/src/option.rs:2577
#![allow(unused)]
fn main() {
    let items = vec![3_u16, 2, 1, 10];

    let mut shared = 0;

    let res: Option<Vec<u16>> = items
        .iter()
        .map(|x| { shared += x; x.checked_sub(2) })
        .collect();

    assert_eq!(res, None);
    assert_eq!(shared, 6);
}
