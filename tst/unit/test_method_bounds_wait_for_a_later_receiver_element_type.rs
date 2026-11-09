trait Separator<S> {
    fn score(separator: S) -> usize;
}

impl<'a> Separator<&'a str> for [u8] {
    fn score(separator: &'a str) -> usize {
        separator.len()
    }
}

impl Separator<u16> for [u16] {
    fn score(separator: u16) -> usize {
        usize::from(separator) + 100
    }
}

struct Buffer<T>(Vec<T>);

impl<T> Buffer<T> {
    fn score<S>(&self, separator: S) -> usize
    where
        [T]: Separator<S>,
    {
        <[T] as Separator<S>>::score(separator)
    }
}

fn main() {
    let mut buffer = Buffer(Vec::new());
    let text = String::from("seven!!");
    let score = buffer.score(text.as_str());
    buffer.0.push(11u8);
    assert_eq!(score, 7);
    assert_eq!(buffer.0, [11]);
}
