//@ compile-fail: cannot transmute between types of different sizes
#[no_mangle]
pub unsafe fn convert(value: u64) -> u8 {
    std::mem::transmute(value)
}
fn main() {}
