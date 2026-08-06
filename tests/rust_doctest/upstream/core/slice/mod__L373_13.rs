// Extracted from library/core/src/slice/mod.rs:373
#![allow(unused)]
fn main() {
    let x = &[0, 1, 2];
    
    if let Some((first, elements)) = x.split_first_chunk::<2>() {
        assert_eq!(first, &[0, 1]);
        assert_eq!(elements, &[2]);
    }
    
    assert_eq!(None, x.split_first_chunk::<4>());
}
