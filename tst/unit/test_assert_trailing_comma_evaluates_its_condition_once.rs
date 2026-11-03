//@ compile-variant: -O
fn main() {
    let mut visits = 0u8;
    assert!({ visits += 1; visits == 1 },);
    assert_eq!(visits, 1);
    assert!((visits == 1),);
}
