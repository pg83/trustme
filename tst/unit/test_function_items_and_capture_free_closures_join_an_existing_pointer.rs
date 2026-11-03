//@ compile-variant: -O
fn first(value: u16) -> u16 { value + 5 }
fn second(value: u16) -> u16 { value + 7 }
fn main() {
    for index in 0..3usize {
        let call = match std::hint::black_box(index) {
            0 => first as fn(u16) -> u16,
            1 => second,
            _ => |value: u16| value + 11,
        };
        assert_eq!(call(653), match index { 0 => 658, 1 => 660, _ => 664 });
    }
}
