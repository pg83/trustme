//@ crate-type: lib
//@ input: support/metadata/own_control_codec_seed.metadata
//@ compile-flags: --extern codec=$DIR/support/metadata/own_control_codec_seed.metadata -Ccodegen-type=monomir -Cemit-cpp-only

extern crate codec;

use codec::stamped as publish;

publish!(FIRST_STAMP);
codec::stamped!(SECOND_STAMP);

const _: [(); 74] = [(); (FIRST_STAMP + SECOND_STAMP) as usize];
pub const TOTAL: u16 = FIRST_STAMP + SECOND_STAMP;
