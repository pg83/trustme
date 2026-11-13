fn bump(value: &mut u16) {
    *value += 1;
}

fn choose(leave: bool) -> u16 {
    if leave {
        bump({
            return 83;
        });
    }
    89
}

fn main() {
    assert_eq!(choose(true), 83);
    assert_eq!(choose(false), 89);
    let mut value = 97;
    bump(&mut value);
    assert_eq!(value, 98);
}
