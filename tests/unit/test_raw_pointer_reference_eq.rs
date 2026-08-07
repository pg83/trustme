fn main() {
    let mut value = 123;
    let const_pointer = &raw const value;
    let mut_pointer = &raw mut value;
    let reference = &value;

    assert!(const_pointer == reference);
    assert!(const_pointer == mut_pointer);
    unsafe {
        assert!(*const_pointer == *reference);
    }
}
