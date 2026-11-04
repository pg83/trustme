//@ compile-fail: error

trait Family { type Value; }
impl<T> Family for (T,) { type Value = T; }

trait Apply<T> { fn selected() -> u32; }
struct Key;
impl<T> Apply<fn(T, <(T,)>::Value) -> T> for Key {
    fn selected() -> u32 { 127 }
}

fn selected<T>() -> u32 where Key: Apply<fn(T, T) -> T> {
    <Key as Apply<fn(T, T) -> T>>::selected()
}

fn main() {
    assert_eq!(selected::<u16>(), 127);
    assert_eq!(selected::<bool>(), 127);
}
