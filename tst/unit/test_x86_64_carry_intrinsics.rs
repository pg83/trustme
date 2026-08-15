#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::{_addcarry_u64, _addcarryx_u64, _subborrow_u64};

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "adx")]
unsafe fn test_carries() {
    let mut value = 0;
    assert_eq!(_addcarry_u64(1, u64::MAX, 0, &mut value), 1);
    assert_eq!(value, 0);

    assert_eq!(_addcarryx_u64(1, u64::MAX, 1, &mut value), 1);
    assert_eq!(value, 1);

    assert_eq!(_subborrow_u64(1, 0, 0, &mut value), 1);
    assert_eq!(value, u64::MAX);
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        test_carries();
    }
}
