//@ compile-variant: -O
fn main() {
    let value: u64;
    unsafe { core::arch::asm!("mov {value}, 41", "xor eax, eax", value = out(reg) value, lateout("eax") _, options(nomem, nostack)); }
    assert_eq!(value, 41);
}
