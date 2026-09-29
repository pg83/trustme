//@ run-pass
//@ aux-build: caller_location_macros.rs
// snapbox's `assert_data_eq!` calls `#[track_caller] Assert::eq`, and a
// failed schemars snapshot panicked "at :0:1". rustc gives a call inside a
// macro expansion the location of the outermost macro invocation
// (`span_as_caller_location`: `outer_expn().expansion_cause()`); a position
// inside an expansion has no source of its own, and it was reported as is.
// A call the caller wrote and a macro only passed on keeps its own place
// (coretests' `location_debug` reads `format!("{:?}", Location::caller())`).
extern crate caller_location_macros;

use caller_location_macros::{remote_here, Assert};

macro_rules! local_here {
    ($value:expr) => {{
        let value = $value;
        Assert.eq(value)
    }};
}

macro_rules! pass_tokens {
    ($($tokens:tt)*) => {
        $($tokens)*
    };
}

macro_rules! outer_here {
    () => {
        local_here!(1)
    };
}

fn main() {
    let direct = Assert.eq(0);
    assert_eq!((direct.file(), direct.line(), direct.column()), (file!(), 34, 25));
    let local = local_here!(1);
    assert_eq!((local.file(), local.line(), local.column()), (file!(), 36, 17));
    let nested = outer_here!();
    assert_eq!((nested.file(), nested.line(), nested.column()), (file!(), 38, 18));
    let passed = pass_tokens!(Assert.eq(3));
    assert_eq!((passed.file(), passed.line(), passed.column()), (file!(), 40, 38));
    let remote = remote_here!(2);
    assert_eq!((remote.file(), remote.line(), remote.column()), (file!(), 42, 18));
}
