#[no_mangle]
pub unsafe extern "C" fn orchard_update(slot: *mut u32, increment: u32) -> u32 {
    let before = unsafe { *slot };
    unsafe { *slot = before.wrapping_add(increment); }
    before.rotate_left(3)
}
