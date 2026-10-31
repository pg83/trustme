union Choice<T: Copy> { item: T, bytes: [u8; 4] }
fn main() {
    unsafe {
    let read = |Choice { item }| item;
    assert_eq!(read(Choice { item: 19u32 }), 19);
    let value = Choice::<u32> { bytes: [2, 3, 5, 7] };
    let Choice { bytes } = value;
    assert_eq!(bytes, [2, 3, 5, 7]);
    }
}
