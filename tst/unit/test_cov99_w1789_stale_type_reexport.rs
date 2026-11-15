//@ crate-type: lib
//@ compile-fail: Couldn't find final component of
//@ aux-build: cov99_w1789_old/cov99_w1789_dependency.rs
//@ aux-output-dir: cov99_w1789_old/cov99_w1789_dependency.rs=old
//@ aux-build: cov99_w1789_export.rs
//@ aux-build: cov99_w1789_new/cov99_w1789_dependency.rs
//@ aux-output-dir: cov99_w1789_new/cov99_w1789_dependency.rs=new

pub fn accept(_: cov99_w1789_export::Alias) {}
