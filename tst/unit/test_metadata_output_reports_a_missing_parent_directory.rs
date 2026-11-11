//@ compile-flags: --emit=metadata --crate-type=rlib -o $OUT/missing/record.rlib
//@ compile-fail: ERROR: can not open

pub const ANSWER: u32 = 37;

pub fn answer() -> u32 {
    ANSWER
}
