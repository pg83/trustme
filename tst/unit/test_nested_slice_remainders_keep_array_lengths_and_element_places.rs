//@ compile-variant: -O

#[inline(never)]
fn inspect(values: &[[u16; 3]]) -> u16 {
    match values {
        [first, middle @ .., last] => first[0] + middle.len() as u16 * 3 + last[2],
        [single] => single[1] * 5,
        [] => 7,
    }
}
fn main() {
    assert_eq!(inspect(&[]), 7);
    assert_eq!(inspect(&[[3, 5, 7]]), 25);
    assert_eq!(inspect(&[[11, 13, 17], [19, 23, 29]]), 40);
    assert_eq!(inspect(&[[3, 5, 7], [11, 13, 17], [19, 23, 29], [31, 37, 41]]), 50);
}
