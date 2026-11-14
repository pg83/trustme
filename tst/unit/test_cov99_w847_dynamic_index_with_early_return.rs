#[inline(never)]
fn select(values: &[u32], position: usize, stop: bool) -> u32 {
    values[if stop { return 0 } else { position }]
}

#[inline(never)]
fn update(values: &mut [u32], position: usize) {
    values[position] += 7;
}

fn main() {
    let mut values = [11, 23, 37];
    assert_eq!(select(&values, 1, false), 23);
    assert_eq!(select(&values, 99, true), 0);
    update(&mut values, 2);
    assert_eq!(select(&values, 2, false), 44);
}
