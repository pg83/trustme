// Extracted from library/core/src/slice/mod.rs:2918
#![allow(unused)]
fn main() {
    let s = [0, 1, 1, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55];
    
    let seek = 13;
    assert_eq!(s.binary_search_by(|probe| probe.cmp(&seek)), Ok(9));
    let seek = 4;
    assert_eq!(s.binary_search_by(|probe| probe.cmp(&seek)), Err(7));
    let seek = 100;
    assert_eq!(s.binary_search_by(|probe| probe.cmp(&seek)), Err(13));
    let seek = 1;
    let r = s.binary_search_by(|probe| probe.cmp(&seek));
    assert!(match r { Ok(1..=4) => true, _ => false, });
}
