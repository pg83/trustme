//@ compile-variant: -O
macro_rules! text { ($ty:ty) => { stringify!($ty) }; }
fn main() {
    let native = text!(unsafe extern "C" fn(u16) -> u32);
    assert!(native.contains("unsafe"));
    assert!(native.contains("extern"));
    assert!(native.contains("\"C\""));
    assert!(native.contains("u16") && native.contains("u32"));
    let plain = text!(unsafe fn() -> u8);
    assert!(plain.contains("unsafe") && plain.contains("u8"));
}
