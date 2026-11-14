//@ aux-build: cov99_w253_missing_leaf.rs
//@ aux-build: cov99_w253_missing_parent.rs
//@ compile-flags: --extern cov99_w253_missing_leaf= --emit=metadata -Cpanic=abort
//@ crate-type: lib
//@ compile-fail: Unable to locate crate 'cov99_w253_missing_leaf' with filename libcov99_w253_missing_leaf.rlib in search directories

#![no_std]

extern crate cov99_w253_missing_parent;

pub fn value() -> u8 {
    cov99_w253_missing_parent::value()
}
