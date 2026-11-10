fn main() {
    let mix = |value: u8| value.rotate_left(1) ^ 5;
    assert_eq!(mix(6), 9);
    println!("{}", mix(6));
}
