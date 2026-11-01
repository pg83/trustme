union Storage<T: Copy> { value: (T, [u16; 2]), bytes: [u8; 16] }

fn read<T: Copy>(storage: Storage<T>) -> (T, u16) {
    unsafe {
        let Storage { value: (item, [first, last]) } = storage;
        (item, first + last)
    }
}

fn main() {
    let first = Storage { value: (19u8, [3, 257]) };
    let second = Storage { value: (0x1234_5678_9abc_def0u64, [7, 11]) };
    assert_eq!(read(std::hint::black_box(first)), (19, 260));
    assert_eq!(read(std::hint::black_box(second)), (0x1234_5678_9abc_def0, 18));
}
