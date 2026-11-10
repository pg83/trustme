trait Origin {
    type Other;
}

trait Pair: Origin
where
    Self::Other: Clone,
    Self::Value: Copy,
{
    type Value;
}

struct Source;
impl Origin for Source {
    type Other = String;
}
impl Pair for Source {
    type Value = u16;
}

fn duplicate<P: Pair>(value: P::Value, other: &P::Other) -> (P::Value, P::Value, P::Other)
where
    P::Other: Clone,
    P::Value: Copy,
{
    (value, value, other.clone())
}

fn main() {
    let text = String::from("projection");
    let (first, second, cloned) = duplicate::<Source>(31, &text);
    assert_eq!(first + second, 62);
    assert_eq!(cloned, text);
}
