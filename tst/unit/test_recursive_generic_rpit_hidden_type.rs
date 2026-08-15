fn recurse(
    mut input: impl Iterator<Item = u32>,
) -> Option<impl Iterator<Item = u32>> {
    if input.next().is_none() {
        recurse(input)
    } else {
        Some(input)
    }
}

fn main() {
    let _ = recurse([1, 2, 3].into_iter());
}
