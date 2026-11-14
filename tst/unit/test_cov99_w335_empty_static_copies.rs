//@ aux-build: cov99_w335_empty_static.rs
//@ crate-type: lib
//@ compile-flags: --emit=metadata

extern crate cov99_w335_empty_static;

pub const FROM_ENCODED: [u8; 0] = cov99_w335_empty_static::EMPTY;

pub static LOCAL_EMPTY: [u8; 0] = [];
pub const FROM_PENDING: [u8; {
    let empty = LOCAL_EMPTY;
    empty.len()
}] = [];
