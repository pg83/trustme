#[inline(never)]
fn read(values: &[u16]) -> u16 {
    match *values {
        [first, second, ..] => first * 10 + second,
        [only] => only,
        [] => 0,
    }
}

#[inline(never)]
fn change(values: &mut [u16]) {
    if let [ref mut first, ref mut second, ..] = *values {
        *first += 3;
        *second += 5;
    }
}

fn main() {
    let mut values = [2, 7, 11];
    assert_eq!(read(&values), 27);
    change(&mut values);
    assert_eq!(values, [5, 12, 11]);
    assert_eq!(read(&values[..1]), 5);
    assert_eq!(read(&[]), 0);
}
