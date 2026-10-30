//@ compile-fail: the placeholder `_` is not allowed
#![feature(pattern_types, pattern_type_macro)]
#![allow(internal_features)]
use std::pat::pattern_type;
fn main() { let _: pattern_type!(_ is 1..=4) = 3u8; }
