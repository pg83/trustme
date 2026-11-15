//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Zstop-after=resolve -Zdump-ast
//@ aux-build: cov99_w1083_v1/cov99_w1083_shapes.rs
//@ aux-output-dir: cov99_w1083_v1/cov99_w1083_shapes.rs=v1
//@ aux-build: cov99_w1083_bridge.rs
//@ aux-build: cov99_w1083_v2/cov99_w1083_shapes.rs
//@ aux-output-dir: cov99_w1083_v2/cov99_w1083_shapes.rs=v2

#![no_std]
#![feature(fn_delegation, trait_alias)]

pub reuse cov99_w1083_bridge::type_identity as delegated_type;
pub reuse cov99_w1083_bridge::trait_identity as delegated_trait;
