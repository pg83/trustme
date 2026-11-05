//@ compile-variant: -O

trait Relation<R, U> {}
impl<T, U> Relation<T, U> for T {}

fn relate_outputs<F, G, A, B, U>(_: F, _: G, tag: U) -> U
where F: FnOnce() -> A, G: FnOnce() -> B, A: Relation<B, U>
{
    tag
}

fn other() -> impl Copy { 31u8 }

fn current() -> impl Copy {
    let tag = relate_outputs(current, other, 13);
    assert_eq!(tag, 13);
    other()
}

fn main() {
    assert_eq!(std::mem::size_of_val(&current()), 1);
    assert_eq!(std::mem::size_of_val(&other()), 1);
}
