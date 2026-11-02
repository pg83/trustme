//@ compile-variant: -O

#[inline(never)]
fn choose(flag: bool) -> usize {
    if flag {
        unsafe { core::arch::asm!("nop", options(nomem, nostack)); }
        17
    } else {
        unsafe { core::arch::asm!("nop", options(nomem, nostack)); }
        43
    }
}

fn main() {
    assert_eq!(choose(true), 17);
    assert_eq!(choose(false), 43);
}
