pub fn bytes(value: i32) -> [u8; std::mem::size_of::<i32>()] {
    value.to_ne_bytes()
}
