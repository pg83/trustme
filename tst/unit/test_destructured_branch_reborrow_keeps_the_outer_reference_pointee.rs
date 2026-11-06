fn choose() -> bool { true }
fn main() {
    let data = Box::new(7i32);
    let one = &data;
    let mut two = &one;
    let (borrowed,): (&_,) = (if choose() { &mut two } else { panic!("unselected") },);
    assert_eq!(****borrowed, 7);
}
