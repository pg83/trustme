#![feature(no_core)]
#![no_core]

#[cfg(all(expect_debug_assertions, not(debug_assertions)))]
compile_error!("debug_assertions must be enabled");

#[cfg(all(expect_no_debug_assertions, debug_assertions))]
compile_error!("debug_assertions must be disabled");

pub fn selected() {}
