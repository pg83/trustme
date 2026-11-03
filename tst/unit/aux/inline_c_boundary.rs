#[inline]
pub extern "C" fn choose(index: usize) -> u32 { [89u32, 97, 101][index] }

#[inline]
pub extern "C" fn checked_add(left: u8, right: u8) -> u8 { left + right }
