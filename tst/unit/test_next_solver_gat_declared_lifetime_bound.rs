//@ check-pass
//@ compile-flags: -Znext-solver

trait Searcher<'a> {
    fn next_match(&mut self) -> Option<(usize, usize)>;
    fn haystack(&self) -> &'a str;
}

trait ReverseSearcher<'a>: Searcher<'a> {
    fn next_match_back(&mut self) -> Option<(usize, usize)>;
}

trait Pattern {
    type Searcher<'a>: Searcher<'a>;
}

struct Matches<'a, P: Pattern>(P::Searcher<'a>);

impl<'a, P: Pattern> Matches<'a, P> {
    fn next_back(&mut self) -> Option<&'a str>
    where
        P::Searcher<'a>: ReverseSearcher<'a>,
    {
        self.0
            .next_match_back()
            .map(|(start, end)| &self.0.haystack()[start..end])
    }
}

fn main() {}
