#[repr(C)]
#[derive(Clone, Copy)]
pub union Bits { pub word: u32, pub bytes: [u8; 4] }
const SAVED: Bits = Bits { word: 0x35271609 };
#[inline(never)]
pub const fn word(value: Bits) -> u32 { unsafe { value.word } }
#[inline(never)]
pub const fn stored() -> u32 { word(SAVED) }
