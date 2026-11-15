//@ aux-build: cov99_w1184_infer_const_type.rs
//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Zstop-after=resolve
#![feature(fn_delegation)]
#![allow(incomplete_features)]

pub reuse cov99_w1184_infer_const_type::idle as forwarded;
