//@ run-pass
// `clone_from` has a default body in the trait, but a caller names it under
// the impl's own type. Where the compiler generates the `Clone` impl -- for an
// array, a tuple or a closure -- the generated impl carries its own, or the
// call has nothing to reach.

use std::clone::Clone;
use std::sync::atomic::{AtomicUsize, Ordering};

static DROPPED: AtomicUsize = AtomicUsize::new(0);

#[derive(PartialEq, Debug)]
struct Counted(u8);

impl Clone for Counted {
    fn clone(&self) -> Self {
        Counted(self.0)
    }
}

impl Drop for Counted {
    fn drop(&mut self) {
        DROPPED.fetch_add(1, Ordering::Relaxed);
    }
}

fn main() {
    let mut copied = Box::new([5, 6, 7]);
    let source = Box::new([8, 9, 10]);
    let addr: *const [i32; 3] = &*copied;
    copied.clone_from(&source);
    assert_eq!(*copied, [8, 9, 10]);
    assert_eq!(addr, &*copied as *const [i32; 3]);

    let mut owned = [String::from("a"), String::from("b")];
    let other = [String::from("c"), String::from("d")];
    owned.clone_from(&other);
    assert_eq!(owned, other);

    let mut pair = (String::from("l"), 1u8);
    let source_pair = (String::from("r"), 2u8);
    pair.clone_from(&source_pair);
    assert_eq!(pair, source_pair);

    // The value the destination held is dropped, once, before the clone of
    // the source takes its place.
    let mut counted = [Counted(1), Counted(2)];
    let replacement = [Counted(3), Counted(4)];
    counted.clone_from(&replacement);
    assert_eq!(DROPPED.load(Ordering::Relaxed), 2);
    assert_eq!(counted, replacement);

    let held = String::from("captured");
    let closure = move || held.len();
    let mut target = closure.clone();
    target.clone_from(&closure);
    assert_eq!(target(), 8);
}
