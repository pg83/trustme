//@ crate-type: lib
//@ edition: 2024

pub fn add_literal() -> usize {
    32usize + 16
}

const THRESHOLD: usize = 32;
pub const SCRATCH_LEN: usize = THRESHOLD + 16;
