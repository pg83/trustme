trait Read<T> {
    fn read(&self) -> T;
}

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

fn main() {
    let value: &dyn Read<u16> = &Source;
    assert_eq!(<dyn Read<u16> as Read<u16>>::read(value), 41);
}
