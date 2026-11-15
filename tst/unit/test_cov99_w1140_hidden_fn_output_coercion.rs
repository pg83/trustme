//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Marker {}

struct Wrapper<F>(F);

impl<F, O, E> Marker for Wrapper<F>
where
    F: Fn() -> Result<O, E>,
{}

fn main() {
    let wrapper = Wrapper(|| Ok::<u8, ()>(1));
    let _: &dyn Marker = &wrapper;
    let _ = Some(()).unwrap();
}
