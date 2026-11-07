use std::mem::{align_of, offset_of, size_of};

#[repr(C)]
struct Natural {
    lead: u8,
    word: u64,
}

#[repr(C, packed(2))]
struct Packed {
    lead: u8,
    word: u64,
}

#[repr(align(16))]
struct Aligned(u8);

#[repr(C)]
struct Custom {
    lead: u8,
    field: Aligned,
    tail: u64,
}

fn main() {
    assert_eq!((size_of::<Natural>(), align_of::<Natural>(), offset_of!(Natural, word)), (16, 8, 8));
    assert_eq!((size_of::<Packed>(), align_of::<Packed>(), offset_of!(Packed, word)), (10, 2, 2));
    assert_eq!((size_of::<Custom>(), align_of::<Custom>(), offset_of!(Custom, field), offset_of!(Custom, tail)), (48, 16, 16, 32));

    let natural = Natural { lead: 3, word: 0x1020_3040_5060_7080 };
    assert_eq!((natural.lead, natural.word), (3, 0x1020_3040_5060_7080));
    let packed = Packed { lead: 5, word: 0x90a0_b0c0_d0e0_f001 };
    let packed_word = packed.word;
    assert_eq!((packed.lead, packed_word), (5, 0x90a0_b0c0_d0e0_f001));
    let custom = Custom { lead: 7, field: Aligned(11), tail: 0x1234_5678_9abc_def0 };
    assert_eq!((custom.lead, custom.field.0, custom.tail), (7, 11, 0x1234_5678_9abc_def0));
}
