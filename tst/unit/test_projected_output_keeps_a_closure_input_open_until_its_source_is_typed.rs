trait Produce {
    type Item;
    fn produce(self) -> Option<Self::Item>;
}

struct Stage<F, I>(F, I);

impl<F, I, R> Produce for Stage<F, I>
where
    I: Iterator,
    F: FnMut(I::Item) -> Option<R>,
{
    type Item = R;
    fn produce(mut self) -> Option<R> {
        self.1.next().and_then(self.0)
    }
}

fn ordered<T: Produce>(stage: T) -> Option<T::Item>
where
    T::Item: Ord,
{
    stage.produce().max(None)
}

fn main() {
    let mut slot = None;
    let first = ordered(Stage(|_| Some(73u8), std::iter::from_fn(|| slot.take())));
    slot = Some(71u16);
    assert_eq!(first, None);
    assert_eq!(slot, Some(71u16));

    let second = ordered(Stage(|value| Some(value + 2u16), std::iter::once(79u16)));
    assert_eq!(second, Some(81u16));
}
