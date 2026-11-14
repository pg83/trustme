//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ proc-macro-aux-build: cov99_w305_const_trait_tokens.rs
#![feature(const_trait_impl)]

// The macro checks serialization and erases the item; this does not test const trait semantics.
#[cov99_w305_const_trait_tokens::check_const_trait]
const trait SerializedConstTrait {}
