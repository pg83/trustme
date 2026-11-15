//@ compile-fail: type annotations needed

#![feature(deref_patterns, deref_pure_trait)]

use std::ops::{Deref, DerefPure};

struct Buffer<T>([T; 0]);

impl Deref for Buffer<u8> {
    type Target = [u8];

    fn deref(&self) -> &[u8] {
        &self.0
    }
}

impl Deref for Buffer<u16> {
    type Target = [u16];

    fn deref(&self) -> &[u16] {
        &self.0
    }
}

unsafe impl DerefPure for Buffer<u8> {}
unsafe impl DerefPure for Buffer<u16> {}

fn main() {
    let buffer: Buffer<_> = Buffer([]);
    match &buffer {
        [] => (),
        _ => (),
    }
}
