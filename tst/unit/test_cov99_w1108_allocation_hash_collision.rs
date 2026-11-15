static VALUES: [&str; 2] = [
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaXzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaYzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
];

#[inline(never)]
fn check(value: &str, middle: u8) {
    assert_eq!(value.len(), 129);
    assert_eq!(value.as_bytes()[64], middle);
}

fn main() {
    check(VALUES[0], b'X');
    check(VALUES[1], b'Y');
}
