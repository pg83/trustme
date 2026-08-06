// Extracted from library/core/src/iter/traits/iterator.rs:1656
#![allow(unused)]
#![feature(iter_map_windows)]
fn main() {
    
    #[derive(Default)]
    struct NonFusedIterator {
        state: i32,
    }
    
    impl Iterator for NonFusedIterator {
        type Item = i32;
    
        fn next(&mut self) -> Option<i32> {
            let val = self.state;
            self.state = self.state + 1;
    
            // yields `0..5` first, then only even numbers since `6..`.
            if val < 5 || val % 2 == 0 {
                Some(val)
            } else {
                None
            }
        }
    }
    
    
    let mut iter = NonFusedIterator::default();
    
    // yields 0..5 first.
    assert_eq!(iter.next(), Some(0));
    assert_eq!(iter.next(), Some(1));
    assert_eq!(iter.next(), Some(2));
    assert_eq!(iter.next(), Some(3));
    assert_eq!(iter.next(), Some(4));
    // then we can see our iterator going back and forth
    assert_eq!(iter.next(), None);
    assert_eq!(iter.next(), Some(6));
    assert_eq!(iter.next(), None);
    assert_eq!(iter.next(), Some(8));
    assert_eq!(iter.next(), None);
    
    // however, with `.map_windows()`, it is fused.
    let mut iter = NonFusedIterator::default()
        .map_windows(|arr: &[_; 2]| *arr);
    
    assert_eq!(iter.next(), Some([0, 1]));
    assert_eq!(iter.next(), Some([1, 2]));
    assert_eq!(iter.next(), Some([2, 3]));
    assert_eq!(iter.next(), Some([3, 4]));
    assert_eq!(iter.next(), None);
    
    // it will always return `None` after the first time.
    assert_eq!(iter.next(), None);
    assert_eq!(iter.next(), None);
    assert_eq!(iter.next(), None);
}
