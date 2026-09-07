/* regex's `expand_bytes`: `dst.extend(caps.get(i).map(|m| m.as_bytes()).unwrap_or(b""))` -
   `unwrap_or`'s `T` is the closure's `&[u8]`, and `b""` unsizes into it. */
struct Match<'t> {
    text: &'t [u8],
    start: usize,
    end: usize,
}

impl<'t> Match<'t> {
    fn as_bytes(&self) -> &'t [u8] {
        &self.text[self.start..self.end]
    }
}

struct Captures<'t> {
    text: &'t [u8],
    locs: Vec<Option<(usize, usize)>>,
}

impl<'t> Captures<'t> {
    fn get(&self, i: usize) -> Option<Match<'t>> {
        self.locs.get(i).copied().flatten().map(|(start, end)| Match { text: self.text, start, end })
    }
}

fn expand(caps: &Captures<'_>, i: usize, dst: &mut Vec<u8>) {
    dst.extend(caps.get(i).map(|m| m.as_bytes()).unwrap_or(b""));
}

fn main() {
    let caps = Captures { text: b"hello world", locs: vec![Some((0, 5)), None] };
    let mut dst = Vec::new();
    expand(&caps, 0, &mut dst);
    expand(&caps, 1, &mut dst);
    expand(&caps, 7, &mut dst);
    assert_eq!(dst, b"hello");
}
