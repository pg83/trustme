//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ proc-macro-aux-build: cov99_w315_passthrough.rs

#[cov99_w315_passthrough::passthrough]
pub fn first([head, ..]: [u8; 2]) -> u8 {
    head
}
