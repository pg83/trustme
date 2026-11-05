//@ compile-fail: cannot transmute between types of different sizes

#[repr(align(32))]
pub struct Empty;

#[no_mangle]
pub unsafe fn convert(value: Empty) -> u16 {
    std::mem::transmute(value)
}

fn main() {}
