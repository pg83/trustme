// Binding an inference variable to a projection is guarded by an occurs check,
// so the variable cannot come to contain itself. Aliased variables share one
// root, so the projection may name any member of the class rather than the
// representative the candidate resolved to; a check that compares the resolved
// types by pointer misses that. The infinite type it builds made every later
// walk recurse until the stack was gone, and this program segfaulted.
use std::collections::VecDeque;

fn main() {
    let mut buf = VecDeque::with_capacity(15);
    buf.extend(0..4);
    assert_eq!(buf.capacity(), 15);
    buf.shrink_to_fit();
    assert!(buf.capacity() >= 4);
    assert_eq!(buf.len(), 4);
}
