// `#[repr(align(N))]` on a union was rejected outright as an unknown repr. It
// raises the union's alignment past its widest member, and the size rounds up
// to match.
//
// Same shape as the upstream tests union/union-align.rs and
// transmutability/unions/repr/should_handle_align.rs.
use std::mem::{align_of, size_of};

#[repr(align(16))]
union Wide {
    a: u8,
    b: u32,
}

// An alignment below the widest member changes nothing.
#[repr(align(2))]
union Narrow {
    a: u64,
}

#[repr(C, align(16))]
union WithC {
    a: u8,
}

fn main() {
    assert_eq!(align_of::<Wide>(), 16);
    assert_eq!(size_of::<Wide>(), 16);

    assert_eq!(align_of::<Narrow>(), 8);
    assert_eq!(size_of::<Narrow>(), 8);

    assert_eq!(align_of::<WithC>(), 16);
    assert_eq!(size_of::<WithC>(), 16);

    // The alignment does not disturb the stored value.
    let w = Wide { b: 0x1234_5678 };
    assert_eq!(unsafe { w.b }, 0x1234_5678);
}
