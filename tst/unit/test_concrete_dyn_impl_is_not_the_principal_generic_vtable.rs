trait Read<T> {
    fn read(&self) -> T;
}
//@ compile-fail: automatically implements
fn nested<T: 'static>(value: &(dyn Read<T> + 'static)) -> (T,)
where
    dyn Read<T>: Read<(T,)>,
{
    <dyn Read<T> as Read<(T,)>>::read(value)
}

struct Source;

impl Read<u16> for Source {
    fn read(&self) -> u16 {
        41
    }
}

impl Read<(u16,)> for dyn Read<u16> {
    fn read(&self) -> (u16,) {
        (<dyn Read<u16> as Read<u16>>::read(self) + 100,)
    }
}

fn main() {
    let value: &(dyn Read<u16> + 'static) = &Source;
    assert_eq!(<dyn Read<u16> as Read<u16>>::read(value), 41);
    assert_eq!(<dyn Read<u16> as Read<(u16,)>>::read(value), (141,));
    assert_eq!(nested(value), (141,));
}
