#![feature(asm_goto_with_outputs)]
#[inline(never)]
fn checkpoint() {
    unsafe { std::arch::asm!("/* {blocked} */", blocked = label { loop {} }, options(nostack)); }
}
#[inline(never)]
fn number() -> u32 {
    let value: u32;
    unsafe {
        std::arch::asm!("mov {value:e}, 61", "/* {blocked} */",
            value = out(reg) value, blocked = label { loop {} }, options(nostack));
    }
    value
}
fn main() {
    let mut visits = 0;
    for _ in 0..3 { checkpoint(); visits += 1; }
    assert_eq!(visits, 3);
    assert_eq!(number(), 61);
}
