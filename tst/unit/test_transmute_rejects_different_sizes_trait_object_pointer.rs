//@ compile-fail: cannot transmute between types of different sizes
trait Value {}
#[no_mangle]
pub unsafe fn convert(value: *const dyn Value) -> *const u8 {
    std::mem::transmute(value)
}
fn main() {}
