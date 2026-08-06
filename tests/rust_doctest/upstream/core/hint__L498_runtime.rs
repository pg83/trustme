// Extracted from library/core/src/hint.rs:498
#![feature(hint_must_use)]

use core::fmt;

pub struct Error(/* ... */);

#[macro_export]
macro_rules! make_error {
    ($($args:expr),*) => {
        core::hint::must_use({
            let error = $crate::make_error(core::format_args!($($args),*));
            error
        })
    };
}

// Implementation detail of make_error! macro.
#[doc(hidden)]
pub fn make_error(args: fmt::Arguments<'_>) -> Error {
    Error(/* ... */)
}

fn demo() -> Option<Error> {
    if true {
        // Oops, meant to write `return Some(make_error!("..."));`
        Some(make_error!("..."));
    }
    None
}

// Make rustdoc not wrap the whole snippet in fn main, so that $crate::make_error works
fn main() {}
