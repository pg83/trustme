#[inline(never)]
fn choose(value: bool) -> u8 {
    let mut condition = false;
    let pointer = &raw mut condition;
    condition = true;
    unsafe { *pointer = value; }
    if condition { 17 } else { 23 }
}

fn main() {
    assert_eq!(choose(std::hint::black_box(true)), 17);
    assert_eq!(choose(std::hint::black_box(false)), 23);
}
