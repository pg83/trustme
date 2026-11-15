struct Tail<T: ?Sized>(T);

trait Family {
    type Data;
    fn data(self) -> Self::Data;
}

impl Family for i32 {
    type Data = [u8; 1];
    fn data(self) -> Self::Data { [11] }
}

impl Family for u32 {
    type Data = [u8; 2];
    fn data(self) -> Self::Data { [22, 23] }
}

fn make<T: Family>(value: T) -> Tail<T::Data> {
    Tail(value.data())
}

fn main() {
    let value = make(0);
    let pointer = &value as *const Tail<[u8]>;
    let tail = unsafe { &(*pointer).0 };
    assert_eq!(tail, &[11u8]);
}
