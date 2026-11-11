//@ crate-type: lib
//@ input: support/metadata/frame_beyond_header.metadata
//@ input: support/metadata/frame_wrapper_beyond_header.metadata
//@ input: support/metadata/own_frame_wrapper.metadata
//@ input: support/metadata/own_control_codec_seed.metadata
//@ input: support/metadata/frame_long_header.py
//@ compile-flags: --extern codec=$DIR/support/metadata/frame_wrapper_beyond_header.metadata -L$DIR/support/metadata -Ccodegen-type=monomir -Cemit-cpp-only
//@ compile-fail: First string does not fit the header buffer:

extern crate codec;

pub const VALUE: u16 = codec::select(13);
