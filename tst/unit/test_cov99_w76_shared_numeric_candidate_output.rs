trait Pick<Tag> {
    type Output;
    fn pick(self, tag: Tag) -> Self::Output;
}

impl<F, R> Pick<i32> for F
where
    F: FnOnce() -> R,
{
    type Output = R;
    fn pick(self, _: i32) -> R { self() }
}

impl<F, R> Pick<u16> for F
where
    F: FnOnce() -> R,
{
    type Output = R;
    fn pick(self, _: u16) -> R { self() }
}

fn choose<Tag, F: Pick<Tag>>(tag: Tag, make: F) -> F::Output {
    make.pick(tag)
}

fn main() {
    let values: Vec<u32> = choose(0, || {
        [Some([3u32, 9]), None]
            .into_iter()
            .filter_map(|value| value)
            .flatten()
    })
    .chain([6u32].into_iter().filter_map(|value| Some(value)))
    .collect();
    assert_eq!(values, [3, 9, 6]);
}
