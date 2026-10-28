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

#[should_panic(expected = "  needle = 3\n")]
#[test]
fn captures_method_argument() {
    let values = [1, 2];
    let needle = 3;
    assert!(values.contains(&needle));
}

#[should_panic(expected = "  value = 4\n")]
#[test]
fn captures_call_object_argument() {
    let predicate = |x: i32| x == 0;
    let value = 4;
    assert!((predicate)(value));
}

#[should_panic(expected = "  index = 1\n")]
#[test]
fn captures_array_index_and_cast() {
    let index = 1;
    let value = 2u8;
    assert!([value as u32, 3u32][index] == 0);
}

#[should_panic(expected = "  value = 5\n")]
#[test]
fn captures_tuple_and_struct_fields() {
    let value = 5;
    assert!((CopyDebug(value), value) == (CopyDebug(0), 0));
}

#[should_panic(expected = "  value = 6\n")]
#[test]
fn captures_if_condition() {
    let value = 6;
    assert!(if value > 0 { false } else { true });
}

#[should_panic(expected = "  value = 7\n")]
#[test]
fn captures_match_scrutinee() {
    let value = 7;
    assert!(match value { 0 => true, _ => false });
}
