#![no_std]

extern crate cov99_w1382_default_body;

pub fn callback() -> fn(u32) -> u32 {
    <u8 as cov99_w1382_default_body::Callback>::echo
}
