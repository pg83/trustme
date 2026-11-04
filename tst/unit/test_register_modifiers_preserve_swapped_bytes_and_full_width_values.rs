//@ compile-variant: -O
fn main() {
    let swapped: u16;
    unsafe {
        core::arch::asm!("xchg {pair:h}, {pair:l}", "add {pair:x}, 3",
            pair = inout(reg_abcd) std::hint::black_box(0x5ba3u16) => swapped,
            options(nomem, nostack));
    }
    assert_eq!(swapped, 0xa35e);
    let mut wide = std::hint::black_box(0x1122_3344_0000_0007u64);
    unsafe {
        core::arch::asm!("add {word:r}, 5", "/* {word:l} {word:x} {word:e} */",
            word = inout(reg) wide, options(nomem, nostack));
    }
    assert_eq!(wide, 0x1122_3344_0000_000c);
}
