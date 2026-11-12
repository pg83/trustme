struct Callback<F>(F);

trait Produce {
    type Item;
    fn produce(self) -> Self::Item;
}

impl<F, R> Produce for Callback<F>
where
    F: FnOnce(u16, u32) -> R,
{
    type Item = R;

    fn produce(self) -> R {
        (self.0)(17, 29)
    }
}

trait Number {
    fn number(self) -> u16;
}

impl Number for u16 {
    fn number(self) -> u16 {
        self
    }
}

fn run<P: Produce>(producer: P) -> u16
where
    P::Item: Number,
{
    producer.produce().number()
}

fn named(_: u16, _: u32) -> u16 {
    67
}

fn main() {
    assert_eq!(run(Callback(|_, _| -> u16 { 61 })), 61);
    assert_eq!(run(Callback(named)), 67);
}
