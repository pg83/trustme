//@ compile-variant: -O
use std::sync::atomic::{AtomicUsize, Ordering};
static DROPPED: AtomicUsize = AtomicUsize::new(0);
struct Stamp(usize);
impl Drop for Stamp {
    fn drop(&mut self) { DROPPED.fetch_add(self.0, Ordering::SeqCst); }
}
enum Container { Packed(Box<(Stamp, Stamp)>), Empty }
#[inline(never)]
fn exercise(take_first: bool, empty: bool) {
    let value = if empty { Container::Empty }
        else { Container::Packed(Box::new((Stamp(1), Stamp(10)))) };
    match value {
        Container::Packed(pair) => {
            if take_first { let first = pair.0; drop(first); }
        }
        Container::Empty => (),
    }
}
fn main() {
    for take_first in [false, true] {
        DROPPED.store(0, Ordering::SeqCst);
        exercise(std::hint::black_box(take_first), false);
        assert_eq!(DROPPED.load(Ordering::SeqCst), 11);
    }
    DROPPED.store(0, Ordering::SeqCst);
    exercise(true, true);
    assert_eq!(DROPPED.load(Ordering::SeqCst), 0);
}
