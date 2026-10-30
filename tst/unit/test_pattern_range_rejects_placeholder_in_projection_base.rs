//@ compile-fail: the placeholder `_` is not allowed
#![feature(pattern_types, pattern_type_macro)]
#![allow(internal_features)]
use std::pat::pattern_type;
struct Wrap<T>(T);
trait Family { type Item; }
impl<T> Family for Wrap<T> { type Item = u8; }
fn main() { let _: pattern_type!(<Wrap<_> as Family>::Item is 1..=4) = 3u8; }
