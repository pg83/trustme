// Extracted from library/core/src/slice/mod.rs:3016
#![allow(unused)]
fn main() {
    let s = [(0, 0), (2, 1), (4, 1), (5, 1), (3, 1),
             (1, 2), (2, 3), (4, 5), (5, 8), (3, 13),
             (1, 21), (2, 34), (4, 55)];
    
    assert_eq!(s.binary_search_by_key(&13, |&(a, b)| b),  Ok(9));
    assert_eq!(s.binary_search_by_key(&4, |&(a, b)| b),   Err(7));
    assert_eq!(s.binary_search_by_key(&100, |&(a, b)| b), Err(13));
    let r = s.binary_search_by_key(&1, |&(a, b)| b);
    assert!(match r { Ok(1..=4) => true, _ => false, });
}
