fn main() {
    let mut first = 17u64;
    let mut second = 23u64;
    unsafe {
        std::arch::asm!(
            "addq $3, {first}", "xorq {first}, {second}",
            "movq $41, %r10", "movq $53, %r11", "fld1", "fstp %st(0)",
            first = inout(reg) first, second = inout(reg) second,
            out("r10") _, out("r11") _, out("st(0)") _,
            options(att_syntax, nomem, nostack),
        );
    }
    assert_eq!((first, second), (20, 3));
}
