struct Functions;

impl Functions {
    fn first() -> u16 { 373 }
    fn second() -> u16 { 379 }
}

fn require_copy<T: Copy>(_: &T) {}

fn main() {
    let selected = match std::hint::black_box(1u8) {
        0 => Functions::first,
        _ => Functions::second,
    };
    require_copy(&selected);
    let callable: fn() -> u16 = selected;
    assert_eq!(callable(), 379);
}
