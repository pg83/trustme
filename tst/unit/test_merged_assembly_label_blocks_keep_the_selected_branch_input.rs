//@ compile-variant: -O
#[inline(never)]
fn select(first: bool, left: u64, right: u64) -> u16 {
    let mut result = 13;
    if first {
        unsafe { std::arch::asm!("test {value}, {value}", "jz {hit}", value = in(reg) left, hit = label { result = 17; }, options(nomem, nostack)); }
    } else {
        unsafe { std::arch::asm!("test {value}, {value}", "jz {hit}", value = in(reg) right, hit = label { result = 17; }, options(nomem, nostack)); }
    }
    result
}
fn main() {
    assert_eq!(select(true, 0, 1), 17);
    assert_eq!(select(false, 0, 1), 13);
    assert_eq!(select(true, 1, 0), 13);
    assert_eq!(select(false, 1, 0), 17);
}
