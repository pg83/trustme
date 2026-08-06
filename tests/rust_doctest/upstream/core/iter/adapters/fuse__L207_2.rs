// Extracted from library/core/src/iter/adapters/fuse.rs:207
#![allow(unused)]
fn main() {
    use std::iter::Fuse;
    #[derive(Default)]
    struct Fourever;
    
    impl Iterator for Fourever {
        type Item = u32;
        fn next(&mut self) -> Option<u32> {
            Some(4)
        }
    }
    
    let mut iter: Fuse<Fourever> = Default::default();
    assert_eq!(iter.next(), Some(4));
}
