fn keep<'a>(value: &'a mut &'a &'a &'a &'a Box<u16>) -> &'a (impl Sized + 'a) {
    value
}

fn main() {
    let value = Box::new(19u16);
    let first = &value;
    let second = &first;
    let third = &second;
    let mut fourth = &third;
    let kept = keep(&mut fourth);
    assert_eq!(std::any::type_name_of_val(kept), std::any::type_name::<&&&&Box<u16>>());
}
