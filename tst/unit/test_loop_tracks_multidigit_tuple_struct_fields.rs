struct Slots(u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, Option<Box<u32>>);
fn main() {
    let mut slots = Slots(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, None);
    let mut index = 0;
    while index < 3 {
        slots.10 = Some(Box::new(index + 7));
        let value = slots.10.take().unwrap();
        assert_eq!(*value, index + 7);
        slots.0 = slots.0 + 2;
        index += 1;
    }
    assert_eq!(slots.0, 6);
    assert!(slots.10.is_none());
}
