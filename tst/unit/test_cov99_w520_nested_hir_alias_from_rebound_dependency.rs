//@ crate-type: lib
//@ compile-flags: -Z stop-after=hir
//@ aux-build: cov99_w520_nested_dependency.rs
//@ aux-output-dir: cov99_w520_nested_dependency.rs=trait-version
//@ aux-build: cov99_w520_nested_alias.rs
//@ aux-build: cov99_w520_alias_version/cov99_w520_nested_dependency.rs
//@ aux-output-dir: cov99_w520_alias_version/cov99_w520_nested_dependency.rs=alias-version

#![feature(trait_alias)]

use cov99_w520_nested_alias::Reach;
