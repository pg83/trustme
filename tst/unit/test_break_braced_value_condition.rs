#![allow(unreachable_code)]

use std::sync::atomic::{AtomicU8, Ordering};

static RESULT: AtomicU8 = AtomicU8::new(0);

fn braced_break_value() {
    loop {
        if (break { RESULT.fetch_add(1, Ordering::Relaxed); }) {
            RESULT.fetch_add(10, Ordering::Relaxed);
        }
    }
}

fn break_before_if_block() {
    loop {
        if break { RESULT.fetch_add(20, Ordering::Relaxed); } {
            RESULT.fetch_add(100, Ordering::Relaxed);
        }
    }
}

fn main() {
    braced_break_value();
    break_before_if_block();
    assert_eq!(RESULT.load(Ordering::Relaxed), 1);
}
