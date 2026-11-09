#[derive(Clone, Copy)]
enum Choice {
    First(u8),
    Second(u8),
}

#[inline(never)]
fn choose(value: Choice) -> u8 {
    let mut choice = Choice::First(0);
    let pointer = &raw mut choice;
    choice = Choice::First(31);
    unsafe { *pointer = value; }
    match choice {
        Choice::First(n) => n,
        Choice::Second(n) => n + 100,
    }
}

fn main() {
    assert_eq!(choose(std::hint::black_box(Choice::First(7))), 7);
    assert_eq!(choose(std::hint::black_box(Choice::Second(11))), 111);
}
