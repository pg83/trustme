fn main() {
    let boxed = Box::new(31u16);
    let mut chain = &&&&boxed;
    let selected: &_ = loop { break &mut chain; };
    assert_eq!(std::any::type_name_of_val(selected), std::any::type_name::<&&&&Box<u16>>());
    assert_eq!(*boxed, 31);
}
