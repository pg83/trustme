macro_rules! ty { ($ty:ty) => { $ty }; }
struct Pair<T>(T, T);

fn take<T>(Pair(first, second): ty!(Pair<T>)) -> (T, T) { (first, second) }

fn main() {
    assert_eq!(take(Pair(11u16, 17u16)), (11, 17));
    let (first, second) = take(Pair(String::from("left"), String::from("right")));
    assert_eq!(first, "left");
    assert_eq!(second, "right");
}
