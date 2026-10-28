#![feature(core_intrinsics)]

fn main() {
    let mut values = [13u16, 29, 47];
    unsafe {
        let shared: &u16 = core::intrinsics::slice_get_unchecked(&values[..], 1);
        assert_eq!(*shared, 29);
        let unique: &mut u16 = core::intrinsics::slice_get_unchecked(&mut values[..], 2);
        *unique += 1;
        let raw: *const u16 = core::intrinsics::slice_get_unchecked(&values[..] as *const [u16], 0);
        assert_eq!(*raw, 13);
        let raw_mut: *mut u16 = core::intrinsics::slice_get_unchecked(&mut values[..] as *mut [u16], 1);
        *raw_mut = 31;
    }
    assert_eq!(values, [13, 31, 48]);
}
