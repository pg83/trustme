fn main() {
    let mut floating = 1.0f64;
    let mut integer = 37u64;
    unsafe {
        std::arch::asm!("btc rax, 63", inout("rax") floating, options(nomem, nostack));
        std::arch::asm!("psllq xmm0, 1", inout("xmm0") integer, options(nomem, nostack));
    }
    assert_eq!(floating, -1.0);
    assert_eq!(integer, 74);
}
