//@ compile-fail: explicit register arguments cannot be referenced
fn main() {
    let result: u32;
    unsafe { core::arch::asm!("mov {0:e}, 281", out("eax") result); }
    assert_eq!(result, 281);
}
