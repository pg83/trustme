#![feature(return_type_notation)]

trait Factory {
    fn make() -> impl Sized;
}

struct Works;

impl Factory for Works {
    fn make() -> impl Sized {}
}

fn require_send<T: Factory>()
where
    T::make(..): Send,
{
}

fn main() {
    require_send::<Works>();
}
