//@ compile-variant: -O
use std::arch::asm;
#[inline(never)]
fn options(flag: bool, value: u64) -> u64 {
    let output;
    unsafe {
        if flag {
            asm!("mov {output}, {value}", output = lateout(reg) output, value = in(reg) value, options(nomem, nostack, preserves_flags));
        } else {
            asm!("mov {output}, {value}", output = lateout(reg) output, value = in(reg) value, options(nomem, nostack));
        }
    }
    output
}
#[inline(never)]
fn parameters(flag: bool, value: u64) -> u64 {
    let output;
    unsafe {
        if flag {
            asm!("lea {output}, [{value} + {offset}]", output = lateout(reg) output, value = in(reg) value, offset = const 7, options(nomem, nostack, preserves_flags));
        } else {
            asm!("lea {output}, [{value} + {offset}]", output = lateout(reg) output, value = in(reg) value, offset = const 11, options(nomem, nostack, preserves_flags));
        }
    }
    output
}
fn main() {
    for flag in [false, true] {
        assert_eq!(options(std::hint::black_box(flag), 45), 45);
        assert_eq!(parameters(std::hint::black_box(flag), 17), if flag { 24 } else { 28 });
    }
}
