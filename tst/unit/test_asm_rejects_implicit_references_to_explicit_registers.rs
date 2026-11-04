//@ compile-fail: explicit register arguments cannot be referenced
fn main() {
    let value: u64;
    unsafe { core::arch::asm!("mov {}, 283", out("rax") value); }
    assert_eq!(value, 283);
}
