//@ compile-variant: -O
use core::arch::asm;

#[inline(never)]
fn instructions(flag: bool, mut value: usize) -> usize {
    unsafe {
        if flag {
            asm!("add {value}, 3", value = inout(reg) value, options(nomem, nostack));
        } else {
            asm!("add {value}, 5", value = inout(reg) value, options(nomem, nostack));
        }
    }
    value
}

#[inline(never)]
fn operands(flag: bool, first: usize, second: usize) -> usize {
    let result;
    unsafe {
        if flag {
            asm!("mov {result}, {input}", result = out(reg) result, input = in(reg) first, options(nomem, nostack));
        } else {
            asm!("mov {result}, {input}", result = out(reg) result, input = in(reg) second, options(nomem, nostack));
        }
    }
    result
}

#[inline(never)]
fn options(flag: bool, mut value: usize) -> usize {
    unsafe {
        if flag {
            asm!("add {value}, 7", value = inout(reg) value, options(nomem, nostack));
        } else {
            asm!("add {value}, 7", value = inout(reg) value, options(readonly, nostack));
        }
    }
    value
}

fn main() {
    for flag in [false, true] {
        assert_eq!(instructions(flag, 11), if flag { 14 } else { 16 });
        assert_eq!(operands(flag, 23, 61), if flag { 23 } else { 61 });
        assert_eq!(options(flag, 31), 38);
    }
}
