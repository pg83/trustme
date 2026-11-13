fn bump(value: &mut u32) {
    *value += 3;
}

fn main() {
    let mut value = 7;
    let reference = &mut value;
    bump('selected: {
        break 'selected reference;
    });
    *reference += 11;
    assert_eq!(value, 21);
}
