#[cfg(target_arch = "x86_64")]
fn main() {
    let input = 41u32;
    let mut shifted: u32;
    let copied: u32;
    let mut jumped = false;
    unsafe {
        std::arch::asm!(
            "add {slot:e}, {step}",
            slot = inout(reg) input => shifted,
            step = const 6,
            options(nomem, nostack),
        );
        std::arch::asm!("add {slot:e}, 2", slot = inout(reg) shifted, options(nomem, nostack));
        std::arch::asm!(
            "mov {target:e}, {source:e}",
            source = in(reg) shifted,
            target = lateout(reg) copied,
            options(nomem, nostack),
        );
        std::arch::asm!("jmp {target}", target = label { jumped = true; });
    }
    assert_eq!((shifted, copied, jumped), (49, 49, true));
}

#[cfg(not(target_arch = "x86_64"))]
fn main() {}
