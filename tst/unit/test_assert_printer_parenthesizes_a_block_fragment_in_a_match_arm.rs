macro_rules! check_arm {
    ($value:expr) => {
        assert!(match 0u8 {
            0 => $value + 1u16,
            _ => 0u16,
        } == 42u16);
    };
}
//@ compile-fail: Unexpected token TOK_PLUS
fn main() {
    let mut calls = 0u16;
    check_arm!({
        calls += 1;
        41u16
    });
    assert_eq!(calls, 1);
}
