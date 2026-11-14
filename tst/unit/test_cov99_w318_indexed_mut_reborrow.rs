fn bump(value: &mut u32) {
    *value += 1;
}

fn main() {
    let mut value = 0_u32;
    let mut slots = [&mut value];
    bump(slots[0]);
    bump(slots[0]);
    assert_eq!(value, 2);
}
