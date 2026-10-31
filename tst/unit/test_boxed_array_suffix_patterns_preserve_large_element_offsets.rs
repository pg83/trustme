fn main() {
    let mut values = Box::new([0_u8; 32769]);
    values[32768] = 37;
    let [.., last] = &*values;
    assert_eq!(*last, 37);
    let [.., last] = &mut *values;
    *last += 5;
    assert_eq!(values[32768], 42);
}
