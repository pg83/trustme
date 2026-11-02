//@ compile-variant: -O
fn copied(values: Box<[u32]>) -> u32 {
    match *values { [first, ..] => first, [] => 0 }
}
fn main() {
    assert_eq!(copied(vec![17, 23].into_boxed_slice()), 17);
    let mut values = vec![String::from("old"), String::from("middle")].into_boxed_slice();
    values[0] = String::from("new");
    let old = std::mem::replace(&mut values[1], String::from("tail"));
    assert_eq!(old, "middle");
    assert_eq!((&values[0][..], &values[1][..]), ("new", "tail"));
    let owned = Box::new([String::from("left"), String::from("right")]);
    let [left, right] = *owned;
    assert_eq!((left, right), (String::from("left"), String::from("right")));
}
