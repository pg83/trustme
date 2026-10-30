#![feature(explicit_tail_calls)]
#![allow(incomplete_features)]
#[inline(never)]
fn finish<T: Copy>(value: T) -> T { value }
#[inline]
pub fn relay<T: Copy>(value: T) -> T { become finish(value) }
