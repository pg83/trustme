fn main() {
    let mut value = 0x2du8;
    let step = 0x19u8;
    let copied: u8;
    unsafe {
        std::arch::asm!("rol {value}, 1", value = inout(reg_byte) value, options(nomem, nostack));
        std::arch::asm!("xor {value}, {step}", value = inlateout(reg_byte) value, step = in(reg_byte) step, options(nomem, nostack));
        std::arch::asm!("mov {copied}, {value}", copied = lateout(reg_byte) copied, value = in(reg_byte) value, options(nomem, nostack, preserves_flags));
    }
    assert_eq!((value, copied), (0x43, 0x43));
}
