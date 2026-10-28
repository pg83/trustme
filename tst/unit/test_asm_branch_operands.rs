use std::arch::asm;

static MARKER: u8 = 29;

#[inline(never)]
fn increment(branch: u8, mut value: u64) -> u64 {
    unsafe {
        match branch {
            0 => asm!("add {value}, {step}", value = inout(reg) value, step = const 3, options(nomem, nostack)),
            1 => asm!("add {value}, {step}", value = inout(reg) value, step = const 3, options(nomem, nostack)),
            _ => asm!("add {value}, {step}", value = inout(reg) value, step = const 4, options(nomem, nostack)),
        }
    }
    value
}

#[inline(never)]
fn address(branch: bool) -> *const u8 {
    let value: *const u8;
    unsafe {
        if branch {
            asm!("lea {value}, [rip + {symbol}]", value = out(reg) value, symbol = sym MARKER, options(nomem, nostack, preserves_flags));
        } else {
            asm!("lea {value}, [rip + {symbol}]", value = out(reg) value, symbol = sym MARKER, options(nomem, nostack, preserves_flags));
        }
    }
    value
}

fn main() {
    for branch in 0..3 {
        assert_eq!(increment(std::hint::black_box(branch), 5), if branch < 2 { 8 } else { 9 });
    }
    assert_eq!(address(std::hint::black_box(false)), &MARKER as *const u8);
    assert_eq!(address(std::hint::black_box(true)), &MARKER as *const u8);
}
