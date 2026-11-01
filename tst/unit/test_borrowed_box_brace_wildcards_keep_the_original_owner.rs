fn main() {
    let mut value = Box::new(41u16);
    match &mut value {
        whole @ Box { .. } => **whole += 2,
    }
    assert_eq!(*value, 43);
    match &value {
        whole @ Box { .. } => assert_eq!(**whole, 43),
    }
}
