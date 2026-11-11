//@ crate-type: lib
#![no_std]
#![crate_name = "own_nested_codec_seed"]

pub struct Wire;

pub enum Mode {
    First,
    Second,
}

pub type Window = [u8; 3];

#[inline]
pub const fn scalar() -> u16 {
    41
}

#[inline]
pub const fn bump(value: u16) -> u16 {
    value + 3
}

pub fn carry(pointer: *const Wire, bytes: Window) -> (*const Wire, Window) {
    (pointer, bytes)
}
