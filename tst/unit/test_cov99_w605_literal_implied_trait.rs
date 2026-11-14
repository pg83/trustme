trait Mark {}

impl Mark for i32 {}

trait Family {
    type Item: Mark;
}

trait Declared: Family
where
    Self::Item: Mark,
{
}

fn accept_mark<T: Mark>(_: T) {}

fn check<T: Declared>() {
    accept_mark(0);
}

struct Concrete;

impl Family for Concrete {
    type Item = i32;
}

impl Declared for Concrete {}

fn main() {
    check::<Concrete>();
}
