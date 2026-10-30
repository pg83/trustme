use std::mem::{align_of, align_of_val, ManuallyDrop};
#[repr(C)]
struct Header<T: ?Sized>(u16, T);
#[repr(C, packed(2))]
struct Tight<T: ?Sized>(u8, ManuallyDrop<T>);
#[repr(align(32))]
struct Aligned(u8);
const WIDE: &Header<dyn Sync> = &Header(3, Aligned(7));
const TIGHT: &Tight<dyn Sync> = &Tight(5, ManuallyDrop::new(19u64));
const WIDE_ALIGN: usize = align_of_val(WIDE);
const TIGHT_ALIGN: usize = align_of_val(TIGHT);
fn main() {
    assert_eq!(WIDE_ALIGN, align_of::<Header<Aligned>>());
    assert_eq!(TIGHT_ALIGN, align_of::<Tight<u64>>());
}
