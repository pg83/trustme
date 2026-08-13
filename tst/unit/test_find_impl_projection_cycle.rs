trait Recursive {
    type Output;
}

impl<T> Recursive for T
where
    T: Recursive,
{
    type Output = <T as Recursive>::Output;
}

trait Selected {}

impl<T> Selected for T where T: Recursive<Output = ()> {}

fn main() {}
