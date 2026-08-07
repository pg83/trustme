#![feature(iter_next_chunk)]
//@ test-harness

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicUsize, Ordering};

struct Tracked<'a>(&'a AtomicUsize);

impl Drop for Tracked<'_> {
    fn drop(&mut self) {
        self.0.fetch_sub(1, Ordering::SeqCst);
    }
}

#[test]
fn next_chunk_drops_partial_array_on_unwind() {
    let alive = AtomicUsize::new(0);
    let mut index = 0;
    let result = catch_unwind(AssertUnwindSafe(|| {
        let mut values = std::iter::from_fn(|| {
            if index == 1 {
                panic!("intended panic");
            }
            index += 1;
            alive.fetch_add(1, Ordering::SeqCst);
            Some(Tracked(&alive))
        });
        let _ = values.next_chunk::<2>();
    }));
    assert!(result.is_err());
    assert_eq!(alive.load(Ordering::SeqCst), 0);
}
