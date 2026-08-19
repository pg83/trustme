//@ run-pass
// A constant may name a `static` that has interior mutability: what the
// constant holds is the address of something the program declared, not a
// temporary the constant would have to keep alive itself.

use core::sync::atomic::{AtomicU8, Ordering};

static SHARED: AtomicU8 = AtomicU8::new(0);

const C: &AtomicU8 = &SHARED;

const PLAIN: &u8 = &7;

fn main() {
    C.store(3, Ordering::Relaxed);
    assert_eq!(SHARED.load(Ordering::Relaxed), 3);
    assert_eq!(*PLAIN, 7);
}
