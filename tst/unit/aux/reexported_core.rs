//@ edition: 2018
// constcat 0.6: a `#![no_std]` 2018 crate re-exports `core` and reaches its
// macros through `$crate::core`. The `extern crate core` the compiler injects
// is named at its definition site, so it neither collides with nor shadows
// the public `use`.
#![no_std]

#[doc(hidden)]
pub use core;

#[macro_export]
macro_rules! concat_via_core {
    ($e:literal) => {
        $crate::core::concat!($e, "!")
    };
}
