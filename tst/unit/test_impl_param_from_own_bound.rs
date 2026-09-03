//@ edition: 2024

// An impl parameter can be determined only by the impl's own where-bound:
// `impl<R, F: FnOnce() -> R> FnOnce<()> for AssertUnwindSafe<F>` binds `F`
// from the self type, and `R` follows from `F: FnOnce() -> R`. If that
// equality is never drawn, `R` stays an inference variable all the way into
// code generation, which has no encoding for one.

use std::panic::{catch_unwind, AssertUnwindSafe};

fn main() {
    let diverging = catch_unwind(AssertUnwindSafe(|| panic!("expected")));
    assert!(diverging.is_err());

    let valued = catch_unwind(AssertUnwindSafe(|| 7u8));
    assert_eq!(valued.unwrap(), 7);
}
