struct Pair(i32, i32);

fn main() {
    let pair = Pair(11, 23);
    let updated = Pair { ..pair };
    assert_eq!((updated.0, updated.1), (11, 23));
}
