//@ compile-variant: -O
#[inline(never)]
fn selected(first: bool, left: u64, right: u64) -> (u64, u64) {
    let output: u64;
    if first {
        unsafe { std::arch::asm!("lea {output}, [{input} + 5]", input = in(reg) left, output = lateout(reg) output, options(pure, nomem, nostack)); }
    } else {
        unsafe { std::arch::asm!("lea {output}, [{input} + 5]", input = in(reg) right, output = lateout(reg) output, options(pure, nomem, nostack)); }
    }
    (output, left ^ right)
}
fn main() {
    assert_eq!(selected(true, 21, 34), (26, 55));
    assert_eq!(selected(false, 21, 34), (39, 55));
    assert_eq!(selected(true, 0, 0), (5, 0));
    assert_eq!(selected(false, 0, 0), (5, 0));
}
