#[repr(C)]
struct Pair {
    earlier: u32,
    later: u32,
}

const fn assign_later(mut pair: Pair) -> Pair {
    pair.earlier = pair.later;
    pair
}

const RESULT: Pair = assign_later(Pair { earlier: 11, later: 29 });

fn main() {
    assert_eq!(RESULT.earlier, 29);
    assert_eq!(RESULT.later, 29);
}
