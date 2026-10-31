fn make() -> impl Fn(u8, i32) -> i64 {
    |small, signed| i64::from(small) + i64::from(signed)
}

fn main() {
    let call = make();
    assert_eq!(call(255, -300), -45);
    assert_eq!(call(7, 1000), 1007);
}
