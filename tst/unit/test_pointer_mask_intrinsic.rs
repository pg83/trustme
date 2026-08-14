#![feature(ptr_mask)]

fn main() {
    let value = 17_u32;
    let pointer = &value as *const u32;
    let tagged = pointer.map_addr(|address| address | 2);
    let masked = tagged.mask(!3);

    assert_eq!(unsafe { *masked }, 17);
}
