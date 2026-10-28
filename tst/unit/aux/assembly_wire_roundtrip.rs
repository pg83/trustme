use std::arch::asm;

pub static MARKER: u8 = 37;

#[inline]
pub fn add<const STEP: u64>(mut value: u64) -> u64 {
    unsafe {
        asm!("add {value}, {step}", value = inout(reg) value, step = const STEP,
            options(pure, nomem, nostack));
    }
    value
}

#[inline]
pub fn address() -> *const u8 {
    let pointer;
    unsafe {
        asm!("lea {pointer}, [rip + {symbol}]", pointer = out(reg) pointer,
            symbol = sym MARKER, options(nomem, nostack, preserves_flags));
    }
    pointer
}

#[inline]
pub fn explicit_register(value: u64) -> u64 {
    let output;
    unsafe {
        asm!("mov rax, {input}", input = in(reg) value, lateout("rax") output,
            options(pure, nomem, nostack, preserves_flags));
    }
    output
}

#[inline]
pub fn jump(mut value: u64) -> u64 {
    unsafe {
        asm!("jmp {target}", target = label { value += 13; }, options(noreturn));
    }
    value
}
