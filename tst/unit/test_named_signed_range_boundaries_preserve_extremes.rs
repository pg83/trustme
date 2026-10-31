const LOW: i8 = -3;
const HIGH: i8 = 3;
fn classify(value: i8) -> u8 {
    match value { ..LOW => 17, LOW..=HIGH => 23, 4.. => 41 }
}
const FIRST: char = 'c';
const LAST: char = 'f';
fn letter(value: char) -> u8 {
    match value { ..FIRST => 17, FIRST..=LAST => 23, _ => 41 }
}
fn main() {
    assert_eq!([classify(i8::MIN), classify(-4), classify(LOW), classify(HIGH), classify(4), classify(i8::MAX)], [17, 17, 23, 23, 41, 41]);
    assert_eq!([letter('a'), letter('c'), letter('f'), letter('z')], [17, 23, 23, 41]);
}
