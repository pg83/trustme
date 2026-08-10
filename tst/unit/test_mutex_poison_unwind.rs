//@ test-harness

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

#[test]
fn mutex_guard_poisoned_during_unwind() {
    let mutex = Mutex::new(1);
    let result = catch_unwind(AssertUnwindSafe(|| {
        let _guard = mutex.lock().unwrap();
        panic!("poison mutex");
    }));

    assert!(result.is_err());
    assert!(mutex.is_poisoned());
}
