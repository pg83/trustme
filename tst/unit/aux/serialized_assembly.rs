#![feature(asm_goto)]
use std::arch::asm;
pub static STAMP: u64 = 37;
#[inline]
pub fn fold<const BIAS: u64>(mut value: u64) -> u64 {
    unsafe { asm!("add rax, {bias}", inout("rax") value, bias = const BIAS, options(nostack)); }
    value
}
#[inline]
pub fn stamp() -> u64 {
    let value;
    unsafe { asm!("mov {value}, qword ptr [rip + {origin}]", value = lateout(reg) value,
        origin = sym STAMP, options(nostack, readonly, preserves_flags)); }
    value
}
#[inline]
pub fn choose<const BIAS: u64>(value: u64) -> u64 {
    unsafe { asm!("test {value}, {value}", "jz {zero}", value = in(reg) value,
        zero = label { return BIAS + 7; }, options(nostack)); }
    BIAS
}
