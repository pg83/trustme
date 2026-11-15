//@ crate-type: lib
//@ aux-build: cov99_w1175_alias_dependency.rs
//@ compile-flags: --crate-name cov99_w1175_alias_dependency
//@ compile-fail: Couldn't find path component 'ImportedFromAux'

extern crate cov99_w1175_alias_dependency as dependency;
use dependency::ImportedFromAux as LocalAlias;
