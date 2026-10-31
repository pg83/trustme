fn main() {
    let mut value = 0x1234u16;
    let extended: u32;
    let mut word = 37u16;
    let mut wide = 41u64;
    unsafe {
        std::arch::asm!("xchg {value:l}, {value:h}", value = inout(reg_abcd) value, options(nomem, nostack, preserves_flags));
        std::arch::asm!("movzx {output:e}, {input:l}", output = out(reg) extended, input = in(reg_abcd) 0x7fu16, options(nomem, nostack, preserves_flags));
        std::arch::asm!("inc {word:x}", "lea {wide:r}, [{wide:r} + 7]", word = inout(reg) word, wide = inout(reg) wide, options(nomem, nostack));
    }
    assert_eq!((value, extended), (0x3412, 0x7f));
    assert_eq!((word, wide), (38, 48));
}
