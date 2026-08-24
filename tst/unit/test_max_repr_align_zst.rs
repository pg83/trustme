use std::mem::{align_of, align_of_val};

const ALIGN: usize = 536_870_912;

#[repr(align(536870912))]
struct MaxAligned;

impl Drop for MaxAligned {
    fn drop(&mut self) {
        assert_eq!(self as *mut MaxAligned as usize % ALIGN, 0);
    }
}

fn main() {
    let value = MaxAligned;
    assert_eq!(align_of::<MaxAligned>(), ALIGN);
    assert_eq!(align_of_val(&value), ALIGN);
    assert_eq!(&value as *const MaxAligned as usize % ALIGN, 0);
    drop(value);
}
