#[inline]
pub unsafe fn u8to64_le(buffer: &[u8], start: usize) -> u64 {
    unsafe { *buffer.get_unchecked(start) as u64 }
}
