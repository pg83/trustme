fn main() {
    let value = 1usize;
    let object: &dyn Send = &value;
    assert_eq!(std::mem::size_of_val(object), std::mem::size_of::<usize>());
    assert_eq!(std::mem::align_of_val(object), std::mem::align_of::<usize>());
}
