fn increment(n: u16) -> u32 { n as u32 + 100 }
static HANDLER: fn(u16) -> u32 = increment;
const HANDLER_ADDRESS: *const fn(u16) -> u32 = &HANDLER;
const FALLBACK: fn(u16) -> u32 = increment;
fn main() {
    assert!(std::ptr::eq(HANDLER_ADDRESS, &HANDLER));
    assert_eq!(HANDLER(23), 123);
    assert_eq!(FALLBACK(37), 137);
    assert_eq!(unsafe { (*HANDLER_ADDRESS)(41) }, 141);
}
