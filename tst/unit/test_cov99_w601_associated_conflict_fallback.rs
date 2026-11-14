trait Pair {
    type First;
    type Second;
}

struct Split;

impl Pair for Split {
    type First = u8;
    type Second = u16;
}

struct Receiver<T>(T);

impl<T, U> Receiver<T>
where
    T: Pair<First = U, Second = U>,
{
    fn choose(&self) -> u8 {
        1
    }
}

trait Fallback {
    fn choose(&self) -> u8;
}

impl Fallback for Receiver<Split> {
    fn choose(&self) -> u8 {
        2
    }
}

fn main() {
    assert_eq!(Receiver(Split).choose(), 2);
}
