//@ run-pass

fn main() {
    #[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
    unsafe {
        let value: u32;
        core::arch::asm!(
            "mov {value:e}, 1 | 2",
            value = lateout(reg) value,
            options(nomem, nostack, preserves_flags),
        );
        assert_eq!(value, 3);
    }
}
