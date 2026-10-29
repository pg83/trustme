use std::arch::asm;
static FIRST: u8 = 17;
static SECOND: u8 = 29;
#[inline(never)]
fn arithmetic(branch: u8, left: u64, right: u64) -> u64 {
    let output;
    unsafe {
        match branch {
            0 => asm!("lea {output}, [{source} + {step}]", step = const 3, source = in(reg) left, output = out(reg) output, options(pure, nomem, nostack, preserves_flags)),
            1 => asm!("lea {output}, [{source} + {step}]", step = const 3, source = in(reg) left, output = out(reg) output, options(pure, nomem, nostack, preserves_flags)),
            2 => asm!("lea {output}, [{source} + {step}]", step = const 3, source = in(reg) right, output = out(reg) output, options(pure, nomem, nostack, preserves_flags)),
            3 => asm!("lea {output}, [{source} + {step}]", step = const 4, source = in(reg) right, output = out(reg) output, options(pure, nomem, nostack, preserves_flags)),
            4 => asm!("lea {output}, [{source} + {step}]", step = const 3, source = inout(reg) left => _, output = out(reg) output, options(pure, nomem, nostack, preserves_flags)),
            5 => asm!("lea {output}, [{source} + {step}]", step = const 3, source = in(reg) left, output = lateout(reg) output, options(pure, nomem, nostack, preserves_flags)),
            _ => asm!("lea {output}, [{source} + {step}]", step = const 3, source = in(reg_abcd) left, output = out(reg) output, options(pure, nomem, nostack, preserves_flags)),
        }
    }
    output
}
#[inline(never)]
fn address(branch: u8) -> *const u8 {
    let output;
    unsafe {
        match branch {
            0 => asm!("lea {output}, [rip + {symbol}]", symbol = sym FIRST, output = out(reg) output, options(pure, nomem, nostack, preserves_flags)),
            1 => asm!("lea {output}, [rip + {symbol}]", symbol = sym FIRST, output = out(reg) output, options(pure, nomem, nostack, preserves_flags)),
            _ => asm!("lea {output}, [rip + {symbol}]", symbol = sym SECOND, output = out(reg) output, options(pure, nomem, nostack, preserves_flags)),
        }
    }
    output
}
#[inline(never)]
fn jump(branch: bool) -> u8 {
    let mut output = 0;
    unsafe {
        if branch {
            asm!("jmp {exit}", exit = label { output = 17; }, options(noreturn));
        } else {
            asm!("jmp {exit}", exit = label { output = 29; }, options(noreturn));
        }
    }
    output
}
fn main() {
    for (branch, expected) in [8, 8, 20, 21, 8, 8, 8].into_iter().enumerate() {
        assert_eq!(arithmetic(std::hint::black_box(branch as u8), 5, 17), expected);
    }
    assert_eq!(address(std::hint::black_box(0)), &FIRST as *const u8);
    assert_eq!(address(std::hint::black_box(1)), &FIRST as *const u8);
    assert_eq!(address(std::hint::black_box(2)), &SECOND as *const u8);
    assert_eq!(jump(std::hint::black_box(true)), 17);
    assert_eq!(jump(std::hint::black_box(false)), 29);
}
