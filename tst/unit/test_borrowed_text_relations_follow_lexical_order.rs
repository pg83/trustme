#[inline(never)]
fn compare(a: &str, b: &str) -> [bool; 6] { [a == b, a != b, a < b, a <= b, a > b, a >= b] }
fn main() {
    let first = String::from("route");
    let next = String::from("router");
    assert_eq!(compare(&first, &next), [false, true, true, true, false, false]);
    assert_eq!(compare(&next, &first), [false, true, false, false, true, true]);
    assert_eq!(compare(&first, "route"), [true, false, false, true, false, true]);
}
