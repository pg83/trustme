struct First {
    bytes: [u8; Self::WIDTH],
}

impl First {
    const WIDTH: usize = 3;
}

struct Second {
    bytes: [u8; Self::WIDTH],
}

impl Second {
    const WIDTH: usize = 5;
}

fn main() {
    let first = First { bytes: [7; 3] };
    let second = Second { bytes: [11; 5] };
    assert_eq!(first.bytes, [7; 3]);
    assert_eq!(second.bytes, [11; 5]);
}
