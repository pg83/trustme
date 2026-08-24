//@ crate-type: lib

#[repr(align(536870912))]
pub struct MaxAligned(pub u8);

#[no_mangle]
pub fn max_aligned_local_address() -> usize {
    let value = MaxAligned(1);
    &value as *const MaxAligned as usize
}
