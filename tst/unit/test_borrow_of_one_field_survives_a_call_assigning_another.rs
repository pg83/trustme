#[inline(never)]
fn next(value: u16) -> u16 { value + 1 }

#[inline(never)]
fn combine(mut pair: (u16, u16)) -> u16 {
    let saved = &pair.0;
    pair.1 = next(pair.1);
    *saved + pair.1
}

fn main() {
    assert_eq!(combine((13, 29)), 43);
}
