// Extracted from library/std/src/sync/once_lock.rs:252
#![feature(once_cell_try_insert)]

use std::sync::OnceLock;

static CELL: OnceLock<i32> = OnceLock::new();

fn main() {
    assert!(CELL.get().is_none());

    std::thread::spawn(|| {
        assert_eq!(CELL.try_insert(92), Ok(&92));
    }).join().unwrap();

    assert_eq!(CELL.try_insert(62), Err((&92, 62)));
    assert_eq!(CELL.get(), Some(&92));
}
