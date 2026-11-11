//@ crate-type: lib
//@ input: support/metadata/own_control_codec_seed.metadata
//@ input: support/metadata/own_truncated_string_count.metadata
//@ input: support/metadata/truncated_string_count.py
//@ compile-flags: --extern codec=$DIR/support/metadata/own_truncated_string_count.metadata
//@ compile-fail: Reader::read - requested 4 bytes at 0 of 3
//@ compile-fail-exit-code: 1

extern crate codec;

pub fn select() -> u16 {
    codec::select(13)
}
