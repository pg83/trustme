#[derive(Clone, Copy)]
struct Fixed(u16);

trait Pair
where
    Fixed: Copy,
    Self::First: Clone,
    Self::Second: Copy,
{
    type First;
    type Second;
}

struct Source;
impl Pair for Source {
    type First = String;
    type Second = u16;
}

fn select<P: Pair>(first: &P::First, second: P::Second, fixed: Fixed)
    -> (P::First, P::Second, P::Second, Fixed)
where
    P::First: Clone,
    P::Second: Copy,
{
    (first.clone(), second, second, fixed)
}

fn main() {
    let text = String::from("siblings");
    let (cloned, first, second, fixed) = select::<Source>(&text, 23, Fixed(7));
    assert_eq!(cloned, text);
    assert_eq!(first + second + fixed.0, 53);
}
