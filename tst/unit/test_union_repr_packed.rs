//@ run-pass
// `#[repr(packed)]` on a union caps every member's alignment, so the union is
// as small and as loosely aligned as the bytes its widest member needs -- the
// padding its natural alignment would have asked for is not there.

#[repr(packed)]
union Packed {
    small: u16,
    bytes: [u8; 3],
}

#[repr(packed(2))]
union PackedTo2 {
    small: u32,
    bytes: [u8; 3],
}

#[repr(C, packed(4))]
union PackedC {
    small: u16,
    bytes: [u8; 3],
}

union Natural {
    small: u16,
    bytes: [u8; 3],
}

const PACKED: Packed = Packed { bytes: [0, 0, 0] };

fn main() {
    use core::mem::{align_of, align_of_val, size_of, size_of_val};

    assert_eq!(size_of::<Packed>(), 3);
    assert_eq!(align_of::<Packed>(), 1);
    assert_eq!(size_of_val(&PACKED), 3);
    assert_eq!(align_of_val(&PACKED), 1);

    assert_eq!(size_of::<PackedTo2>(), 4);
    assert_eq!(align_of::<PackedTo2>(), 2);

    assert_eq!(size_of::<PackedC>(), 4);
    assert_eq!(align_of::<PackedC>(), 2);

    assert_eq!(size_of::<Natural>(), 4);
    assert_eq!(align_of::<Natural>(), 2);
}
