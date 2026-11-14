fn main() {
    let values: [u8; 2] = [3, #[cfg(any())] 99, 5];
    assert_eq!(values, [3, 5]);
}
