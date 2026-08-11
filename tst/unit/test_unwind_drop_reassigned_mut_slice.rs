//@ compile-flags: -O

#[inline(never)]
fn repeatedly_narrow(mut values: &mut [u8], mut count: usize) {
    while count != 0 && !values.is_empty() {
        let midpoint = values.len() / 2;
        values = &mut values[midpoint..];
        count -= 1;
    }
}

fn main() {
    let mut values = [1, 2, 3, 4, 5, 6, 7, 8];
    repeatedly_narrow(&mut values, 3);
    assert_eq!(values, [1, 2, 3, 4, 5, 6, 7, 8]);
}
