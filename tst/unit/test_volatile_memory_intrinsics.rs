#![feature(core_intrinsics)]

use std::intrinsics::{
    unaligned_volatile_load, unaligned_volatile_store, volatile_copy_memory,
    volatile_copy_nonoverlapping_memory, volatile_set_memory,
};

fn main() {
    let mut bytes = [1_u8, 2, 3, 4, 5];

    unsafe {
        volatile_copy_memory(bytes.as_mut_ptr().add(1), bytes.as_ptr(), 4);
    }
    assert_eq!(bytes, [1, 1, 2, 3, 4]);

    let source = [9_u8, 8];
    unsafe {
        volatile_copy_nonoverlapping_memory(bytes.as_mut_ptr(), source.as_ptr(), source.len());
        volatile_set_memory(bytes.as_mut_ptr().add(2), 0x7f, 2);
    }
    assert_eq!(bytes, [9, 8, 0x7f, 0x7f, 4]);

    let words = [0x1122_u16, 0x3344];
    let mut copied_words = [0_u16; 2];
    unsafe {
        volatile_copy_nonoverlapping_memory(copied_words.as_mut_ptr(), words.as_ptr(), 2);
        volatile_set_memory(copied_words.as_mut_ptr(), 0xab, 2);
    }
    assert_eq!(copied_words, [0xabab, 0xabab]);

    let mut unaligned = [0_u8; 5];
    let pointer = unsafe { unaligned.as_mut_ptr().add(1).cast::<u32>() };
    unsafe {
        unaligned_volatile_store(pointer, 0x4433_2211);
        assert_eq!(unaligned_volatile_load(pointer), 0x4433_2211);
    }
    assert_eq!(&unaligned[1..], &0x4433_2211_u32.to_ne_bytes());

    let mut dst_zst = ();
    let src_zst = ();
    unsafe {
        volatile_copy_memory(&mut dst_zst, &src_zst, 100);
        volatile_copy_nonoverlapping_memory(&mut dst_zst, &src_zst, 100);
        volatile_set_memory(&mut dst_zst, 0, 100);
    }
}
