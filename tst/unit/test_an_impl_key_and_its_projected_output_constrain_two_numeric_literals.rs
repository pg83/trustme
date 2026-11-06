struct Source<T>(std::marker::PhantomData<T>);
trait Select<Key> { type Output; }
impl<T> Select<u16> for Source<T> { type Output = T; }

fn connect<Key, Value>(key: Key, value: Value) -> (Key, Value)
where Source<Value>: Select<Key, Output = Key> {
    (key, value)
}

fn main() {
    let pair = connect(5, 7);
    assert_eq!(std::any::type_name_of_val(&pair), std::any::type_name::<(u16, u16)>());
    assert_eq!(pair, (5u16, 7u16));
}
