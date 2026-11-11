//@ crate-type: lib
#![no_std]
#![crate_name = "own_control_codec_seed"]

pub struct Bounded<T: Copy, const N: usize> {
    pub items: [T; N],
}

#[inline]
pub const fn select(value: u8) -> u16 {
    match value {
        2 => 17,
        7 => 23,
        11 => 29,
        13 => 31,
        _ => 37,
    }
}

#[inline]
pub const fn increment(value: u16) -> u16 {
    value + 5
}

pub fn opaque() -> impl Copy {
    13u16
}

#[cfg(target_arch = "x86_64")]
#[inline(always)]
pub unsafe fn register_echo(value: u64) -> u64 {
    let result;
    unsafe {
        core::arch::asm!(
            "mov {0}, {1}",
            out(reg) result,
            in(reg) value,
            options(pure, nomem, nostack)
        );
    }
    result
}

#[macro_export]
macro_rules! stamped {
    ($name:ident) => {
        pub const $name: u16 = 37;
    };
}
