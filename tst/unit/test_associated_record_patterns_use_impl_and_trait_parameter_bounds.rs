//@ compile-variant: -O
#![feature(more_qualified_paths)]
struct Record<T> { value: T }
struct Wrapper<T>(T);
trait Project { type Output; }
impl<T> Project for Wrapper<T> { type Output = Record<T>; }
impl<T> Wrapper<T> {
    fn extract(record: <Wrapper<T> as Project>::Output) -> T {
        let <Wrapper<T> as Project>::Output { value } = record;
        value
    }
}
trait Reader<T> {
    fn read(record: <Wrapper<T> as Project>::Output) -> T {
        let <Wrapper<T> as Project>::Output { value } = record;
        value
    }
}
struct Gauge;
impl<T> Reader<T> for Gauge {}
fn main() {
    assert_eq!(Wrapper::<u16>::extract(Record { value: 887 }), 887);
    assert_eq!(<Gauge as Reader<char>>::read(Record { value: 'Ю' }), 'Ю');
}
