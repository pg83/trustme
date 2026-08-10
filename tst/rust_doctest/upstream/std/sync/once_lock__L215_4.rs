// Extracted from library/std/src/sync/once_lock.rs:215
use std::sync::OnceLock;

static CELL: OnceLock<i32> = OnceLock::new();

fn main() {
    assert!(CELL.get().is_none());

    std::thread::spawn(|| {
        assert_eq!(CELL.set(92), Ok(()));
    }).join().unwrap();

    assert_eq!(CELL.set(62), Err(62));
    assert_eq!(CELL.get(), Some(&92));
}
