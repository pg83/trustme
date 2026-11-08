#[inline(always)]
pub fn choose(take_label: bool) -> u32 {
    unsafe {
        core::arch::asm!(
            "test {flag:e}, {flag:e}",
            "jnz {hit}",
            flag = in(reg) take_label as u32,
            hit = label { return 29; },
            options(nomem, nostack),
        );
    }
    17
}
