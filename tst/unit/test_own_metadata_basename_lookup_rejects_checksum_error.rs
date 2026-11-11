//@ crate-type: lib
//@ input: support/metadata/frame_wrapper_checksum.metadata
//@ input: support/metadata/frame_early_checksum.metadata
//@ input: support/metadata/own_frame_wrapper.metadata
//@ input: support/metadata/own_control_codec_seed.metadata
//@ input: support/metadata/frame_failures.py
//@ input: support/metadata/frame_name_routes.py
//@ compile-flags: --extern codec=$DIR/support/metadata/frame_wrapper_checksum.metadata -L$DIR/support/metadata -Ccodegen-type=monomir -Cemit-cpp-only
//@ compile-fail: Unable to decompress

extern crate codec;

pub const VALUE: u16 = codec::select(13);
