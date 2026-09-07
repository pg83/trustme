/* bytemuck's `try_cast_slice_mut`: `pod` is `&mut [?B]` from a generic call; the closure
   `|pod| B::is_valid_bit_pattern(pod)` under `all` gives `?B = <B as CheckedBitPattern>::Bits`
   and only then is `pod.as_mut_ptr() as *mut B` cast (upstream checks casts last). */
pub trait AnyBitPattern: Copy + 'static {}
pub trait CheckedBitPattern: Copy {
    type Bits: AnyBitPattern;
    fn is_valid_bit_pattern(bits: &Self::Bits) -> bool;
}
impl AnyBitPattern for u8 {}
impl CheckedBitPattern for bool {
    type Bits = u8;
    fn is_valid_bit_pattern(bits: &u8) -> bool {
        *bits < 2
    }
}
#[derive(Debug)]
pub enum CastError {
    Size,
    InvalidBitPattern,
}
unsafe fn raw_cast_slice_mut<A: Copy, B: Copy>(a: &mut [A]) -> Result<&mut [B], CastError> {
    if core::mem::size_of::<A>() != core::mem::size_of::<B>() {
        return Err(CastError::Size);
    }
    Ok(core::slice::from_raw_parts_mut(a.as_mut_ptr() as *mut B, a.len()))
}
pub fn try_cast_slice_mut<A: Copy, B: CheckedBitPattern>(a: &mut [A]) -> Result<&mut [B], CastError> {
    let pod = unsafe { raw_cast_slice_mut(a) }?;
    if pod.iter().all(|pod| <B as CheckedBitPattern>::is_valid_bit_pattern(pod)) {
        Ok(unsafe { core::slice::from_raw_parts_mut(pod.as_mut_ptr() as *mut B, pod.len()) })
    } else {
        Err(CastError::InvalidBitPattern)
    }
}
fn main() {
    let mut a = [0u8, 1, 1];
    let b: &mut [bool] = try_cast_slice_mut(&mut a).unwrap();
    assert_eq!(b, &[false, true, true]);
    let mut c = [0u8, 2];
    assert!(try_cast_slice_mut::<u8, bool>(&mut c).is_err());
}
