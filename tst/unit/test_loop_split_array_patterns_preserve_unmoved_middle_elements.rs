//@ compile-variant: -O
use std::sync::atomic::{AtomicUsize, Ordering};

static COUNTS: [AtomicUsize; 12] = [const { AtomicUsize::new(0) }; 12];
struct Mark(usize);
impl Drop for Mark {
    fn drop(&mut self) { COUNTS[self.0].fetch_add(1, Ordering::SeqCst); }
}

fn main() {
    {
        let mut slot = [Mark(0), Mark(1), Mark(2), Mark(3)];
        for round in 0..2 {
            let [first, .., last] = slot;
            drop(first);
            drop(last);
            let base = 4 + round * 4;
            slot = [Mark(base), Mark(base + 1), Mark(base + 2), Mark(base + 3)];
        }
    }
    for count in &COUNTS { assert_eq!(count.load(Ordering::SeqCst), 1); }
}
