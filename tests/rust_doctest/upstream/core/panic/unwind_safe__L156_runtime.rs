// Extracted from library/core/src/panic/unwind_safe.rs:156
#![allow(unused)]
fn main() {
    use std::panic::{self, AssertUnwindSafe};

    let mut variable = 4;
    let other_capture = 3;

    let result = {
        let mut wrapper = AssertUnwindSafe(&mut variable);
        panic::catch_unwind(move || {
            **wrapper += other_capture;
        })
    };
    // ...
}
