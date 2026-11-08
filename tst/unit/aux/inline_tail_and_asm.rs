#![feature(explicit_tail_calls)]
#[inline(never)]
pub fn next(value: u32) -> u32 { value + 1 }
#[inline(always)]
pub fn tail(value: u32) -> u32 { become next(value) }
#[inline(always)]
pub fn unchanged(value: u32) -> u32 {
    unsafe {
        core::arch::asm!("/* {value} */", value = in(reg) value,
            options(nomem, nostack, preserves_flags));
    }
    value
}
