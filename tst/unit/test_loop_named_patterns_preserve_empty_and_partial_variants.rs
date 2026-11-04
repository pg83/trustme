//@ compile-variant: -O
use std::sync::atomic::{AtomicUsize, Ordering};

static COUNTS: [AtomicUsize; 4] = [const { AtomicUsize::new(0) }; 4];
struct Mark(usize);
impl Drop for Mark {
    fn drop(&mut self) { COUNTS[self.0].fetch_add(1, Ordering::SeqCst); }
}
enum Choice { Empty, Value { first: Mark, second: Mark } }

fn main() {
    {
        let mut slot = Choice::Value { first: Mark(0), second: Mark(1) };
        for round in 0..3 {
            match slot {
                Choice::Empty {} => (),
                Choice::Value { first, .. } => drop(first),
            }
            slot = if round == 0 {
                Choice::Value { first: Mark(2), second: Mark(3) }
            } else {
                Choice::Empty
            };
        }
    }
    for count in &COUNTS { assert_eq!(count.load(Ordering::SeqCst), 1); }
}
