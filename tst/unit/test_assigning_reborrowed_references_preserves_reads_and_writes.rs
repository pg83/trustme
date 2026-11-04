//@ compile-variant: -O
#[inline(never)]
fn bump(mut value: &mut u16) {
    for _ in 0..4 {
        value = &mut *value;
        *value += 3;
    }
}

#[inline(never)]
fn read(mut value: &u16) -> u16 {
    value = &*value;
    value = value;
    *value
}

#[inline(never)]
fn keep(value: &mut (u16, u16)) {
    *value = *value;
    value.0 = value.0;
    value.1 = value.1;
}

fn main() {
    let mut pair = (29, 47);
    keep(&mut pair);
    assert_eq!(pair, (29, 47));
    bump(&mut pair.0);
    assert_eq!(pair, (41, 47));
    assert_eq!(read(&pair.0), 41);
}
