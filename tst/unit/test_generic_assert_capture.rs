//@ test-harness

#![feature(generic_assert)]

use std::fmt::{Debug, Formatter};

#[derive(Clone, Copy, PartialEq)]
struct CopyDebug(i32);

impl Debug for CopyDebug {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> Result<(), std::fmt::Error> {
        formatter.write_str("captured")
    }
}

#[should_panic(expected = "Assertion failed: value == CopyDebug(2)\nWith captures:\n  value = captured\n")]
#[test]
fn captures_local() {
    let value = CopyDebug(1);
    assert!(value == CopyDebug(2));
}

#[should_panic(expected = "Assertion failed: left == right\nWith captures:\n  left = captured\n  right = captured\n")]
#[test]
fn captures_multiple_locals() {
    let left = CopyDebug(1);
    let right = CopyDebug(2);
    assert!(left == right);
}

fn consume(_: CopyDebug) -> bool {
    false
}

#[should_panic(expected = "Assertion failed: consume(value)\nWith captures:\n  value = captured\n")]
#[test]
fn captures_consumed_local() {
    let value = CopyDebug(1);
    assert!(consume(value));
}
