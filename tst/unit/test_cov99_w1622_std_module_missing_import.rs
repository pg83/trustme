//@ edition: 2015
//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: Unable to resolve `use` target

mod std {}

use std::missing;
