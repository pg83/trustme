//@ crate-type: lib
//@ input: support/metadata/own_control_codec_seed.metadata
//@ compile-flags: --extern codec=$DIR/support/metadata/own_control_codec_seed.metadata -Ccodegen-type=monomir -Cemit-cpp-only -O

extern crate codec;

const FIRST: u16 = codec::select(7);
const SECOND: u16 = codec::increment(2);
const _: [(); 23] = [(); FIRST as usize];
const _: [(); 7] = [(); SECOND as usize];

pub type Bytes = codec::Bounded<u8, 3>;
pub const VALUES: (u16, u16) = (FIRST, SECOND);

pub fn opaque_value() -> impl Copy {
    codec::opaque()
}

#[cfg(target_arch = "x86_64")]
pub unsafe fn asm_copy(value: u64) -> u64 {
    unsafe { codec::register_echo(value) }
}
