//@ compile-flags: -Coverflow-checks=off
fn main() {
    let a = 37u16;
    let b = 17u16;
    let unsigned = [a + b, a - b, a * b, a / b, a % b, a & b, a | b, a ^ b, a << 1, a >> 1];
    let a = -37i32;
    let b = 17i32;
    let signed = [a + b, a - b, a * b, a / b, a % b, a & b, a | b, a ^ b, a << 1, a >> 1];
    let a = 7.5f32;
    let b = 2.0f32;
    let float = [a + b, a - b, a * b, a / b, a % b];
    let a = true;
    let b = false;
    let flags = (a & b, a | b, a ^ b);
    assert_eq!(unsigned, [54, 20, 629, 2, 3, 1, 53, 52, 74, 18]);
    assert_eq!(signed, [-20, -54, -629, -2, -3, 17, -37, -54, -74, -19]);
    assert_eq!(float, [9.5, 5.5, 15.0, 3.75, 1.5]);
    assert_eq!(flags, (false, true, true));
}
