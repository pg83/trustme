// Extracted from library/core/src/iter/traits/exact_size.rs:51
#![allow(unused)]
fn main() {
    struct Counter {
        count: usize,
    }
    impl Counter {
        fn new() -> Counter {
            Counter { count: 0 }
        }
    }
    impl Iterator for Counter {
        type Item = usize;
        fn next(&mut self) -> Option<Self::Item> {
            self.count += 1;
            if self.count < 6 {
                Some(self.count)
            } else {
                None
            }
        }
    }
    impl ExactSizeIterator for Counter {
        // We can easily calculate the remaining number of iterations.
        fn len(&self) -> usize {
            5 - self.count
        }
    }
    
    // And now we can use it!
    
    let mut counter = Counter::new();
    
    assert_eq!(5, counter.len());
    let _ = counter.next();
    assert_eq!(4, counter.len());
}
