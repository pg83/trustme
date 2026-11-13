fn transform(mut value: i32, shift: u32) -> i32 {
    value += 11;
    value -= 5;
    value *= 3;
    value /= 3;
    value %= 17;
    value ^= 6;
    value &= 14;
    value |= 1;
    value <<= shift;
    value >>= 1;
    value = value + 2;
    value
}

fn main() {
    let first = transform(37, 2);
    let second = transform(5, 1);
    assert_eq!(first, 32);
    assert_eq!(second, 15);
    assert!(second < first);
    assert_eq!(-first, -32);
    let mut flag = true;
    flag &= true;
    flag |= false;
    flag ^= true;
    assert!(!flag);
    assert_eq!(*&first, 32);
}
