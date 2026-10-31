fn make() -> impl Fn((i16, i16)) -> bool {
    |(left, right)| left < right
}

fn main() {
    let call = make();
    assert!(call((-7, 3)));
    assert!(!call((3, -7)));
    assert!(!call((3, 3)));
}
