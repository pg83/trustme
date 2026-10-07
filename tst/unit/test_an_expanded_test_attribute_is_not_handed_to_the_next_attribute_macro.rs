//@ test-harness
//@ proc-macro-aux-build: serial_like.rs
use serial_like::serial;
use std::sync::atomic::{AtomicUsize, Ordering};

static RUNS: AtomicUsize = AtomicUsize::new(0);

fn run_serial(function: fn()) {
    function();
}

#[test]
#[serial]
fn registered_once() {
    assert_eq!(RUNS.fetch_add(1, Ordering::SeqCst), 0);
}
