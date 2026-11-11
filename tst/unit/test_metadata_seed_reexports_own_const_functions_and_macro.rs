//@ crate-type: lib
//@ input: support/metadata/own_control_codec_seed.metadata
//@ compile-flags: --extern codec=$DIR/support/metadata/own_control_codec_seed.metadata -Ccodegen-type=monomir -Cemit-cpp-only
#![no_std]
#![crate_name = "own_frame_wrapper"]

extern crate codec;

pub use codec::{increment, select, stamped, Bounded};
