//@ compile-fail: type annotations needed
trait Reader {
    type Value;
}

struct Direct<T>(T);
impl<T: Copy> Reader for Direct<T> {
    type Value = T;
}

struct Indirect<T>(T);
impl<T> Reader for Indirect<T>
where
    Direct<T>: Reader,
{
    type Value = <Direct<T> as Reader>::Value;
}

fn select<R: Reader<Value = u32>>() {}

fn main() {
    select();
}
