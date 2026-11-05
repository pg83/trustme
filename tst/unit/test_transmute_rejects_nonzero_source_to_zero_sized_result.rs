//@ compile-fail: cannot transmute between types of different sizes

#[no_mangle]
pub unsafe fn convert(value: u32) -> [u64; 0] {
    std::mem::transmute(value)
}

fn main() {}
