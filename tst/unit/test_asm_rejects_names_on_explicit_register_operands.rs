//@ compile-fail: explicit register arguments cannot have names
fn main() {
    let value: u32;
    unsafe { core::arch::asm!("mov {result:e}, 293", result = out("eax") value); }
    assert_eq!(value, 293);
}
