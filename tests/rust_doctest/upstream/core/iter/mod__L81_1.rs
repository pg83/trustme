// Extracted from library/core/src/iter/mod.rs:81
#![allow(unused)]
fn main() {
    // First, the struct:

    /// An iterator which counts from one to five
    struct Counter {
        count: usize,
    }

    // we want our count to start at one, so let's add a new() method to help.
    // This isn't strictly necessary, but is convenient. Note that we start
    // `count` at zero, we'll see why in `next()`'s implementation below.
    impl Counter {
        fn new() -> Counter {
            Counter { count: 0 }
        }
    }

    // Then, we implement `Iterator` for our `Counter`:

    impl Iterator for Counter {
        // we will be counting with usize
        type Item = usize;

        // next() is the only required method
        fn next(&mut self) -> Option<Self::Item> {
            // Increment our count. This is why we started at zero.
            self.count += 1;

            // Check to see if we've finished counting or not.
            if self.count < 6 {
                Some(self.count)
            } else {
                None
            }
        }
    }

    // And now we can use it!

    let mut counter = Counter::new();

    assert_eq!(counter.next(), Some(1));
    assert_eq!(counter.next(), Some(2));
    assert_eq!(counter.next(), Some(3));
    assert_eq!(counter.next(), Some(4));
    assert_eq!(counter.next(), Some(5));
    assert_eq!(counter.next(), None);
}
