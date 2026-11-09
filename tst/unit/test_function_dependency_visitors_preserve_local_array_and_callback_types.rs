#[inline(never)]
fn increment(value: u16) -> u16 {
    value + 1
}

#[inline(never)]
fn factory() -> (fn(u16) -> u16, [u16; 2]) {
    let callback: fn(u16) -> u16 = increment;
    let values = [3u16, 5];
    (callback, values)
}

fn main() {
    let (callback, values) = factory();
    assert_eq!(callback(values[0]) + values[1], 9);
}
