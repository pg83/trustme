#[no_mangle]
pub extern "C" fn coverage_native_scale(value: u32) -> u32 {
    value.wrapping_mul(3).wrapping_add(5)
}
