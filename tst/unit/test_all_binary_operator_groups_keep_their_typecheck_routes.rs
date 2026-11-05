//@ compile-variant: -O

fn arithmetic(a: u32, b: u32) -> [u32; 10] {
    [a + b, a - b, a * b, a / b, a % b,
     a & b, a | b, a ^ b, a << b, a >> b]
}

fn comparisons(a: i32, b: i32) -> [bool; 6] {
    [a == b, a != b, a < b, a <= b, a > b, a >= b]
}

fn main() {
    assert_eq!(arithmetic(19, 3), [22, 16, 57, 6, 1, 3, 19, 16, 152, 2]);
    assert_eq!(comparisons(-7, 5), [false, true, true, true, false, false]);
    let mut visited = 0;
    assert!(!(false && { visited += 1; true }));
    assert!(true || { visited += 1; false });
    assert!(true && { visited += 2; true });
    assert!(false || { visited += 4; true });
    assert_eq!(visited, 6);
}
