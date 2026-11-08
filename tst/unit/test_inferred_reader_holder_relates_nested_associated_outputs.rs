trait Reader {
    type Value: Copy;
    fn read(self) -> Self::Value;
}

struct Leaf<T>(T);
impl<T: Copy> Reader for Leaf<T> {
    type Value = T;
    fn read(self) -> T { self.0 }
}

struct Mapped<R>(R);
impl<R: Reader> Reader for Mapped<R> {
    type Value = R::Value;
    fn read(self) -> Self::Value { self.0.read() }
}

struct Selected<R>(R);
impl<R: Reader> Reader for Selected<R> {
    type Value = <Mapped<R> as Reader>::Value;
    fn read(self) -> Self::Value { Mapped(self.0).read() }
}

struct Holder<R: Reader<Value = u32>>(R);

fn main() {
    let Holder(first) = Holder(Leaf(23));
    let Holder(second) = Holder(Mapped(Leaf(31)));
    let Holder(third) = Holder(Selected(Leaf(37)));
    assert_eq!(first.read(), 23u32);
    assert_eq!(second.read(), 31u32);
    assert_eq!(third.read(), 37u32);
}
