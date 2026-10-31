static DATA: [u8; 3] = [17, 23, 41];

fn selected(flag: u32) -> u32 {
    unsafe {
        std::arch::asm!(
            "cmpl ${value}, {flag:e}", "je {matched}",
            flag = in(reg) flag, value = const 37,
            matched = label { return 41; }, options(att_syntax, nostack),
        );
    }
    17
}

fn main() {
    let address: *const u8;
    let dollar: *const u8;
    unsafe {
        std::arch::asm!(
            "leaq {data}+{offset}(%rip), {address}",
            data = sym DATA, offset = const 1, address = out(reg) address,
            options(att_syntax, readonly, nostack),
        );
        std::arch::asm!(
            "leaq 2f(%rip), {dollar}", "jmp 3f", "2: .ascii \"$\"", "3:",
            dollar = out(reg) dollar, options(att_syntax, nostack, preserves_flags),
        );
        assert_eq!((*address, *dollar), (23, b'$'));
    }
    assert_eq!((selected(17), selected(37)), (17, 41));
}
