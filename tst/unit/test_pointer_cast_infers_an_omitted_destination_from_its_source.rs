fn main() {
    let value = 37u32;
    let source = &value as *const u32;
    let destination = source as *const _;
    assert_eq!(destination as usize, source as usize);
    assert_eq!(std::any::type_name_of_val(&destination), "*const u32");
}
