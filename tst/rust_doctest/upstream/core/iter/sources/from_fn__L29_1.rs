// Extracted from library/core/src/iter/sources/from_fn.rs:29
#![allow(unused)]
fn main() {
    let mut count = 0;
    let counter = std::iter::from_fn(move || {
        // Increment our count. This is why we started at zero.
        count += 1;

        // Check to see if we've finished counting or not.
        if count < 6 {
            Some(count)
        } else {
            None
        }
    });
    assert_eq!(counter.collect::<Vec<_>>(), &[1, 2, 3, 4, 5]);
}
