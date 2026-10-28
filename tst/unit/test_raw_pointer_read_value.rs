fn main() {
    let value = 1;
    let pointer = &value as *const i32;
    assert_eq!(unsafe { *pointer }, 1);
}
