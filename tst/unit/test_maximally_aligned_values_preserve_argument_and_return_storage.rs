//@ crate-type: lib

#[repr(C, align(536870912))]
pub struct Wide { pub value: u64 }

#[no_mangle]
#[inline(never)]
pub fn forward_wide(value: Wide) -> Wide { value }

#[no_mangle]
#[inline(never)]
pub fn read_wide(value: Wide) -> u64 { value.value }

const _: () = {
    assert!(std::mem::align_of::<Wide>() == 536870912);
    assert!(std::mem::size_of::<Wide>() == 536870912);
};
