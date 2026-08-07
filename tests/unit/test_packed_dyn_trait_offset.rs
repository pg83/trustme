use std::mem::ManuallyDrop;
use std::ptr::addr_of;

#[repr(C, packed(2))]
struct Packed<T: ?Sized>(u8, ManuallyDrop<T>);

fn main() {
    let packed = Packed(0, ManuallyDrop::new(1usize));
    let sized: &Packed<usize> = &packed;
    let sized_offset = unsafe { addr_of!(sized.1).cast::<u8>().offset_from(addr_of!(sized.0)) };
    let wide: &Packed<dyn Send> = sized;
    let wide_offset = unsafe { addr_of!(wide.1).cast::<u8>().offset_from(addr_of!(wide.0)) };

    assert_eq!(sized_offset, wide_offset);
}
