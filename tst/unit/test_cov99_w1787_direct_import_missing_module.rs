//@ edition: 2021
//@ aux-build: cov99_w1787_direct_dep.rs
//@ aux-output-dir: cov99_w1787_direct_dep.rs=original-dependency
//@ aux-build: cov99_w1787_direct_facade.rs
//@ aux-build: replacement/cov99_w1787_direct_dep.rs
//@ aux-output-dir: replacement/cov99_w1787_direct_dep.rs=replacement-dependency
//@ compile-flags: --emit=metadata -C panic=abort
//@ compile-only
//@ compile-fail: Couldn't find path component 'inner'

fn accept(_: cov99_w1787_direct_facade::Token) {}

fn main() {}
