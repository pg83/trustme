#![feature(asm_goto_with_outputs)]

#[inline(always)]
pub fn select(value: u32) -> u32 {
    let result: u32;
    unsafe {
        core::arch::asm!(
            "test {value:e}, {value:e}",
            "jz {zero}",
            "mov {result:e}, {value:e}",
            "add {result:e}, 107",
            value = in(reg) value,
            result = lateout(reg) result,
            zero = label { return 109; },
            options(nomem, nostack)
        );
    }
    result
}
