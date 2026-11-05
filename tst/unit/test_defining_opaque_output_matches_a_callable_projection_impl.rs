//@ compile-variant: -O
#![feature(unboxed_closures)]

trait OutputOf<F, U> {}
impl<F, U> OutputOf<F, U> for <F as FnOnce<()>>::Output
where F: FnOnce<()>
{}

fn check_outputs<F, G, U>(_: F, _: G, tag: U) -> U
where F: FnOnce<()>, G: FnOnce<()>, F::Output: OutputOf<G, U>
{
    tag
}

fn other() -> impl Copy { 59u8 }

fn current() -> impl Copy {
    assert_eq!(check_outputs(current, other, 11), 11);
    other()
}

fn main() {
    assert_eq!(std::mem::size_of_val(&current()), 1);
}
