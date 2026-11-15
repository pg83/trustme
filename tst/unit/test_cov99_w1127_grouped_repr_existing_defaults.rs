//@ edition: 2021
//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Zstop-after=expand --emit=metadata -Cpanic=abort
//@ compile-fail: Unknown struct repr 'u8'

#[repr(transparent, C, packed, u8)]
pub struct ConflictingRepr(pub u8);
