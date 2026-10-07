//@ test-harness
//@ proc-macro-aux-build: serial_like.rs
use serial_like::serial;

fn run_serial(function: fn()) {
    function();
}

fn run_serial_returning<E>(function: fn() -> Result<(), E>) -> Result<(), E> {
    function()
}

#[test]
#[serial]
fn returns_nothing() {
    assert_eq!(1 + 1, 2);
}

#[test]
#[serial]
fn returns_a_result() -> Result<(), String> {
    Ok(())
}
