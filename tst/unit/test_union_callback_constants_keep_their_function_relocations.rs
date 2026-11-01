//@ compile-variant: -O
static OFFSET: u16 = 11;
fn target(value: u16) -> u16 { value + OFFSET }
union Callback { function: fn(u16) -> u16, bits: usize }
const CALLBACK: Callback = Callback { function: target };
#[inline(never)]
fn invoke(value: u16) -> u16 { unsafe { (CALLBACK.function)(value) } }
fn main() {
    assert_eq!(invoke(7), 18);
    assert_eq!(invoke(13), 24);
}
