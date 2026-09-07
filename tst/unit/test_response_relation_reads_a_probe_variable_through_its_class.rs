/* Proving `FlatMap<I::IntoIter, FromFn<F>, G>: Iterator` binds `FromFn<F>::Item` to
   `Word` through a variable the nested probe created and unified with the closure's
   return variable.  A relation on that probe-born variable must be read through the
   class it joined - the caller's variable - or dropped; exported as it was, it named a
   variable the caller's table does not have. */
#[derive(Debug, PartialEq)]
struct Word<'a> {
    word: &'a str,
    width: usize,
}

fn split_words<'a, I>(words: I, splits: &'a [usize]) -> impl Iterator<Item = Word<'a>>
where
    I: IntoIterator<Item = Word<'a>>,
{
    words.into_iter().flat_map(move |word| {
        let mut prev = 0;
        let mut points = splits.iter().copied();
        std::iter::from_fn(move || {
            if let Some(idx) = points.next() {
                let w = Word { word: &word.word[prev..idx], width: idx - prev };
                prev = idx;
                return Some(w);
            }
            if prev < word.word.len() {
                let w = Word { word: &word.word[prev..], width: word.word.len() - prev };
                prev = word.word.len() + 1;
                return Some(w);
            }
            None
        })
    })
}

fn main() {
    let words = vec![Word { word: "foobar", width: 6 }];
    let parts: Vec<Word> = split_words(words, &[3]).collect();
    assert_eq!(parts, vec![Word { word: "foo", width: 3 }, Word { word: "bar", width: 3 }]);
}
