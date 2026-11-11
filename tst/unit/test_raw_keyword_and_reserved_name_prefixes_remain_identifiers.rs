fn r#type(r#_value: u16, r#crate_value: u16) -> u16 {
    r#_value + r#crate_value
}

fn main() {
    let r#self_value = 5;
    let r#Self_value = 7;
    let r#super_value = 11;
    assert_eq!(r#type(2, 3) + r#self_value + r#Self_value + r#super_value, 28);
}
