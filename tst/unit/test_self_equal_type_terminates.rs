// Comparing a type with itself has to terminate. Types are interned, so the
// two sides of this comparison are the same pointer; without saying so the
// comparison walks the type's parameters and, where a parameter leads back to
// the type, recurses until the stack is gone. This program made the compiler
// segfault.

use std::collections::VecDeque;

fn main() {
    let mut buf = VecDeque::with_capacity(15);
    buf.extend(0..4);
    assert_eq!(buf.capacity(), 15);
    buf.shrink_to_fit();
    assert!(buf.capacity() >= 4);
    assert_eq!(buf.len(), 4);
}
