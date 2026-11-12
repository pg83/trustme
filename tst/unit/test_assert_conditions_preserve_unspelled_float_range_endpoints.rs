fn main() {
    assert!((1.25..2.5).contains(&1.75));
    assert!((-0.5..=3.75).contains(&3.75));
    assert!(!(1.25..2.5).contains(&2.5));
    assert!((1.0..).contains(&2.0));
}
