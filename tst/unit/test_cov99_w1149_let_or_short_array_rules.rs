fn first(values: [u8; 32]) -> u8 {
    let ([first, 0, ..] | [first, ..]) = values;
    first
}

fn main() {
    assert_eq!(first([0; 32]), 0);
    assert_eq!(first([7; 32]), 7);
}
