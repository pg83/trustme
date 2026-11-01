//@ compile-variant: -O
static mut RESULT: u16 = 0;
static BASE: u16 = 11;
#[inline(never)]
fn produce(value: u16) -> u16 { BASE + value * 3 }
#[inline(never)]
fn store(value: u16) { unsafe { RESULT = produce(value); } }
fn main() {
    store(7);
    assert_eq!(unsafe { RESULT }, 32);
    store(13);
    assert_eq!(unsafe { RESULT }, 50);
}
