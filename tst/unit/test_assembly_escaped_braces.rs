use std::arch::asm;
fn main() {
    let result: u32;
    unsafe { asm!("/* {{ literal }} */", "mov {0:e}, 23", out(reg) result, options(nomem, nostack)); }
    assert_eq!(result, 23);
}
