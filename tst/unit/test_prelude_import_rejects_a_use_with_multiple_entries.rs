//@ compile-fail: #[prelude_import] should be on a single-entry use
#![feature(prelude_import)]

#[prelude_import]
use std::{fmt, mem};

fn main() {}
