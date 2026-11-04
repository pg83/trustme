//@ compile-variant: -O

trait Family { type Value; }
impl<T> Family for (T,) { type Value = T; }

trait Apply<T> { fn selected() -> u32; }
struct Key;
impl<T> Apply<fn(T, <(T,) as Family>::Value) -> T> for Key {
    fn selected() -> u32 { 131 }
}

fn selected<T>() -> u32 where Key: Apply<fn(T, T) -> T> {
    <Key as Apply<fn(T, T) -> T>>::selected()
}

fn main() {
    assert_eq!(selected::<u16>(), 131);
    assert_eq!(selected::<bool>(), 131);
}
