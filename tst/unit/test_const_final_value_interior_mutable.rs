//@ compile-fail: A constant may not refer to interior mutable data
// A constant's final value may not point at a temporary the program can change
// through it: the temporary is kept for the life of the program, and every use
// of the constant would share it.

use core::sync::atomic::AtomicU8;

const C: &AtomicU8 = &AtomicU8::new(0);

fn main() {
    let _ = C;
}
