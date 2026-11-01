fn main() {
    let value = Box::new(17u16);
    let result = match value { Box { .. } => 23u16 };
    assert_eq!(result, 23);
    assert_eq!(*value, 17);
}
