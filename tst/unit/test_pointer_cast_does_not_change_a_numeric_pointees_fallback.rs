fn main() {
    let value = 37;
    let pointer = &value as *const _;
    let bytes = pointer as *const u8;
    assert_eq!(std::mem::size_of_val(&value), 4);
    assert_eq!(pointer as usize, bytes as usize);
    assert_eq!(unsafe { *pointer }, 37);
}
