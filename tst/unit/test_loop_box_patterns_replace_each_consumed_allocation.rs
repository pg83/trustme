//@ compile-variant: -O
#![feature(box_patterns)]
use std::sync::atomic::{AtomicUsize, Ordering};

static COUNTS: [AtomicUsize; 3] = [const { AtomicUsize::new(0) }; 3];
struct Mark(usize);
impl Drop for Mark {
    fn drop(&mut self) { COUNTS[self.0].fetch_add(1, Ordering::SeqCst); }
}

fn main() {
    {
        let mut slot = Box::new(Mark(0));
        for round in 0..2 {
            let box first = slot;
            drop(first);
            slot = Box::new(Mark(round + 1));
        }
    }
    for count in &COUNTS { assert_eq!(count.load(Ordering::SeqCst), 1); }
}
