//@ compile-fail: error:

trait Evidence<T> {}

struct Source;

impl Evidence<u16> for Source {}
impl Evidence<u32> for Source {}

trait Choose {
    type Output;
    fn choose() -> Option<Self::Output>;
}

impl<T> Choose for Source
where
    Source: Evidence<T>,
{
    type Output = T;

    fn choose() -> Option<T> {
        None
    }
}

fn main() {
    let _ = Source::choose();
}
