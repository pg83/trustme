fn main() {
    let value = Box::new(17u16);
    let identify = |argument: &_| std::any::type_name_of_val(argument);
    let actual = identify(&mut &&&&&value);
    assert_eq!(actual, std::any::type_name::<&&&&&Box<u16>>());
}
