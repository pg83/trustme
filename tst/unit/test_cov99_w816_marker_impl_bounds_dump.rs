//@ crate-type: lib
//@ compile-flags: -Zdump-hir
//@ output-contains: _2_hir.rs where

pub struct Envelope<T>(pub T);

unsafe impl<T: Send> Send for Envelope<T> {}
