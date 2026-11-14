//@ aux-build: cov99_w537\backslash_dep.rs
//@ aux-output-dir: cov99_w537\backslash_dep.rs=t-aux
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort --extern 'cov99_w537_dep=$OUT-aux/libcov99_w537\backslash_dep.rlib'
#![no_std]

pub type Imported = cov99_w537_dep::Marker;
