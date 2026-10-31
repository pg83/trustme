#[inline(never)]
fn plus(value: u32) -> u32 { value + 7 }
#[inline(never)]
fn times(value: u32) -> u32 { value * 3 }
#[inline(never)]
fn choose(first: fn(u32) -> u32, second: fn(u32) -> u32, flag: bool, value: u32) -> u32 {
    if flag { first(value) } else { second(value) }
}
fn main() {
    assert_eq!(choose(plus, times, std::hint::black_box(true), 11), 18);
    assert_eq!(choose(plus, times, std::hint::black_box(false), 11), 33);
}
