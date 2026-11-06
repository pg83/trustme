trait Project { type Value; fn value(self) -> Self::Value; }
impl<V> Project for (u16, V) {
    type Value = V;
    fn value(self) -> V { self.1 }
}
fn project<P: Project>(pair: P) -> P::Value { pair.value() }
fn same<T>(left: T, _: T) -> T { left }
fn main() {
    let head = Default::default();
    let tail = 3;
    let value = same(head, project((head, tail)));
    assert_eq!(std::any::type_name_of_val(&value), "u16");
    assert_eq!(value, 0);
}
