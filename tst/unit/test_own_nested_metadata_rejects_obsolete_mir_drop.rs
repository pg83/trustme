//@ crate-type: lib
//@ input: support/metadata/obsolete_mir_drop.metadata
//@ input: support/metadata/own_nested_codec_seed.metadata
//@ input: support/metadata/nested_tag_mutations.py
//@ compile-flags: --extern codec=$DIR/support/metadata/obsolete_mir_drop.metadata -Ccodegen-type=monomir -Cemit-cpp-only
//@ compile-fail: Obsolete MIR drop statement in crate metadata

extern crate codec;

const FIRST: u16 = codec::scalar();
const SECOND: u16 = codec::bump(2);
const _: [(); 41] = [(); FIRST as usize];
const _: [(); 5] = [(); SECOND as usize];

pub type Shapes = (*const codec::Wire, codec::Window, codec::Mode);
pub const VALUES: (u16, u16) = (FIRST, SECOND);
