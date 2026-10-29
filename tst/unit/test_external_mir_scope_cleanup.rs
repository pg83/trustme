//@ aux-build: mir_scope_cleanup.rs
use std::cell::Cell;
use std::panic::{catch_unwind, panic_any, AssertUnwindSafe};

fn main() {
    for first in [true, false] {
        let log = Cell::new(0);
        let expected = if first { 132 } else { 231 };
        let before_tail = mir_scope_cleanup::scoped(&log, first, || {});
        assert_eq!(before_tail, expected / 10);
        assert_eq!(log.get(), expected);
        log.set(0);
        let error = catch_unwind(AssertUnwindSafe(|| {
            mir_scope_cleanup::scoped(&log, first, || panic_any(7u8));
        })).unwrap_err();
        assert_eq!(*error.downcast::<u8>().unwrap(), 7);
        assert_eq!(log.get(), expected);
    }
}
