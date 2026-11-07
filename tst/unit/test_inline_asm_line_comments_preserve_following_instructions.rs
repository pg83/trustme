fn main() {
    let first: u32;
    let second: u32;
    unsafe {
        core::arch::asm!(
            "mov {value:e}, 977 // initial value\nadd {value:e}, 6 // final value",
            value = lateout(reg) first,
            options(nostack, nomem),
        );
        core::arch::asm!(
            "mov {value:e}, 1019 // trailing comment without newline",
            value = lateout(reg) second,
            options(nostack, nomem),
        );
    }
    assert_eq!(first, 983);
    assert_eq!(second, 1019);
}
