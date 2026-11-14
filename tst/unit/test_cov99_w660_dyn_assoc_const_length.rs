trait Element {
    type Item;
}

trait Length {
    const N: usize;
}

impl Length for dyn Element<Item = u8> {
    const N: usize = 1;
}

fn main() {
    let value: [u8; <dyn Element<Item = _> as Length>::N] = [7];
    assert_eq!(value[0], 7);
}
