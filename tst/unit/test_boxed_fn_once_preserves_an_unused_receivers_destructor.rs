#![feature(fn_traits, unboxed_closures)]

use std::sync::atomic::{AtomicUsize, Ordering};

static DROPPED: AtomicUsize = AtomicUsize::new(0);

struct Action(usize);

impl Drop for Action {
    fn drop(&mut self) {
        DROPPED.fetch_add(self.0, Ordering::SeqCst);
    }
}

impl FnOnce<()> for Action {
    type Output = u32;

    extern "rust-call" fn call_once(self, (): ()) -> u32 {
        37
    }
}

fn main() {
    let boxed: Box<dyn FnOnce() -> u32> = Box::new(Action(7));
    assert_eq!(DROPPED.load(Ordering::SeqCst), 0);
    assert_eq!(boxed(), 37);
    assert_eq!(DROPPED.load(Ordering::SeqCst), 7);

    assert_eq!(Action(11)(), 37);
    assert_eq!(DROPPED.load(Ordering::SeqCst), 18);
}
