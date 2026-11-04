pub fn rotated(input: [u16; 3]) -> [u16; 3] {
    [input[1], input[2], input[0]]
}

pub fn values(first: u16) -> impl ExactSizeIterator<Item = u16> + Clone {
    [first, first + 1].into_iter()
}
