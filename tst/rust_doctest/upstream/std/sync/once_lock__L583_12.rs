// Extracted from library/std/src/sync/once_lock.rs:583
use std::sync::OnceLock;

fn main() {
    assert_eq!(OnceLock::<()>::new(), OnceLock::default());
}
