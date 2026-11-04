//@ compile-variant: -O
use std::sync::atomic::{AtomicUsize, Ordering};

static COUNTS: [AtomicUsize; 6] = [const { AtomicUsize::new(0) }; 6];
struct Mark(usize);
impl Drop for Mark {
    fn drop(&mut self) { COUNTS[self.0].fetch_add(1, Ordering::SeqCst); }
}
enum Choice { Left(Mark, Mark), Right(Mark, Mark) }

fn main() {
    {
        let mut slot = Choice::Left(Mark(0), Mark(1));
        for round in 0..2 {
            let (Choice::Left(first, _) | Choice::Right(first, _)) = slot;
            drop(first);
            slot = if round == 0 {
                Choice::Right(Mark(2), Mark(3))
            } else {
                Choice::Left(Mark(4), Mark(5))
            };
        }
    }
    for count in &COUNTS { assert_eq!(count.load(Ordering::SeqCst), 1); }
}
