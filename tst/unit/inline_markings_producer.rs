#[inline]
#[no_mangle]
pub fn trustme_inline_normal_probe(value: u32) -> u32 {
    value.wrapping_add(1)
}

#[inline(always)]
#[no_mangle]
pub fn trustme_inline_always_probe(value: u32) -> u32 {
    value.wrapping_mul(2)
}
