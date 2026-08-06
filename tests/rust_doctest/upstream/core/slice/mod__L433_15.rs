// Extracted from library/core/src/slice/mod.rs:433
#![allow(unused)]
fn main() {
    let x = &[0, 1, 2];
    
    if let Some((elements, last)) = x.split_last_chunk::<2>() {
        assert_eq!(elements, &[0]);
        assert_eq!(last, &[1, 2]);
    }
    
    assert_eq!(None, x.split_last_chunk::<4>());
}
