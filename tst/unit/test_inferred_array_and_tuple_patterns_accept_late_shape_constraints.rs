fn main() {
    let [first, last] = std::array::from_fn(|i| i as u32 + 2);
    assert_eq!(first * 10 + last, 23);
    let split = |[first, middle @ .., last]: [u32; 4]| first + middle[0] + middle[1] + last;
    assert_eq!(split([2, 3, 5, 7]), 17);
    let tuple = |(first, .., last): (u32, u32, u32)| first + last;
    assert_eq!(tuple((11u32, 13u32, 17u32)), 28);
}
