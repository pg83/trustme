//@ crate-type: lib
//@ input: support/metadata/own_nested_codec_seed.metadata
//@ compile-flags: --extern codec=$DIR/support/metadata/own_nested_codec_seed.metadata -Ccodegen-type=monomir -Cemit-cpp-only

extern crate codec;

const FIRST: u16 = codec::scalar();
const SECOND: u16 = codec::bump(2);
const _: [(); 41] = [(); FIRST as usize];
const _: [(); 5] = [(); SECOND as usize];

pub type Shapes = (*const codec::Wire, codec::Window, codec::Mode);
pub const VALUES: (u16, u16) = (FIRST, SECOND);
