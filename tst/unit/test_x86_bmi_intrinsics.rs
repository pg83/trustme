#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::{
    _bextr2_u32, _bextr2_u64, _bzhi_u32, _bzhi_u64, _pdep_u32, _pdep_u64, _pext_u32,
    _pext_u64,
};

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "bmi1,bmi2")]
unsafe fn test_bmi() {
    let value = 0b1101_0110;
    let control = 1 | (4 << 8);

    assert_eq!(_bextr2_u32(value, control), 0b1011);
    assert_eq!(_bzhi_u32(value, 5), 0b10110);
    assert_eq!(_pext_u32(value, 0b1111_0000), 0b1101);
    assert_eq!(_pdep_u32(0b1101, 0b1111_0000), 0b1101_0000);

    assert_eq!(_bextr2_u64(value.into(), control.into()), 0b1011);
    assert_eq!(_bzhi_u64(value.into(), 5), 0b10110);
    assert_eq!(_pext_u64(value.into(), 0b1111_0000), 0b1101);
    assert_eq!(_pdep_u64(0b1101, 0b1111_0000), 0b1101_0000);
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        test_bmi();
    }
}
