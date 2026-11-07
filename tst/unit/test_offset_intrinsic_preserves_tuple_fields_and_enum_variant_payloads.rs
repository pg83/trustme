#![feature(intrinsics, rustc_attrs)]
#![allow(internal_features)]

#[rustc_intrinsic]
const fn offset_of<T>(variant: u32, field: u32) -> usize;

#[repr(C)]
enum Choice {
    First { small: u16, large: u32 },
    Second { marker: u8, word: u64 },
}

const WORD_OFFSET: usize = offset_of::<Choice>(1, 1);

fn main() {
    let value = Choice::Second { marker: 7, word: 487 };
    let base = &value as *const Choice as usize;
    let actual = match &value {
        Choice::Second { word, .. } => word as *const u64 as usize - base,
        _ => 0,
    };
    assert_eq!(WORD_OFFSET, actual);
    assert_eq!(offset_of::<(u8, u32)>(0, 1), std::mem::offset_of!((u8, u32), 1));
}
