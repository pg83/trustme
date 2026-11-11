//@ crate-type: lib
//@ input: support/metadata/frame_early_checksum.metadata
//@ input: support/metadata/own_control_codec_seed.metadata
//@ input: support/metadata/frame_failures.py
//@ compile-flags: --extern codec=$DIR/support/metadata/frame_early_checksum.metadata -Ccodegen-type=monomir -Cemit-cpp-only
//@ compile-fail: Unable to decompress /

extern crate codec;

const CHOICE: u16 = codec::select(13);
const NEXT: u16 = codec::increment(4);
const _: [(); 31] = [(); CHOICE as usize];
const _: [(); 9] = [(); NEXT as usize];

codec::stamped!(HEADER_MARK);
const _: [(); 37] = [(); HEADER_MARK as usize];

pub type Bytes = codec::Bounded<u8, 3>;
pub const VALUES: (u16, u16) = (CHOICE, NEXT);
