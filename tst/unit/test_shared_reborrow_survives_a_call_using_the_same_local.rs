#[inline(never)]
fn inspect(value: &u16) -> u16 { *value + 1 }

#[inline(never)]
fn combine(value: u16) -> u16 {
    let saved = &value;
    let next = inspect(&value);
    *saved + next
}

fn main() {
    assert_eq!(combine(19), 39);
}
