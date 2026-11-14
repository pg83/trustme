//@ proc-macro-aux-build: cov99_w304_const_impl_tokens.rs
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(const_trait_impl)]

trait Marker {}
struct Subject;

#[cov99_w304_const_impl_tokens::check_const_impl]
impl const Marker for Subject {}
