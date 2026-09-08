//@ run-pass
// `flags.iter().filter_map(|f| f.s.long).chain(opts.iter().filter_map(|o| o.s.long))`
// (clap 2.33 `suggestions.rs`): `chain`'s `U: IntoIterator<Item = Self::Item>` puts
// `<FilterMap<Iter<Opt>, ..> as Iterator>::Item` against
// `<FilterMap<Iter<Flag>, ..> as Iterator>::Item` while both closure returns are
// still open.  Upstream normalizes each to a fresh variable with the projection
// left as an obligation, so the pair says nothing about the two `FilterMap`s
// themselves; reading it as their self types being equal mismatched
// `FlagBuilder` against `OptBuilder` and dropped the only `chain`.
struct Switched<'a> {
    long: Option<&'a str>,
}
struct FlagBuilder<'a> {
    s: Switched<'a>,
}
struct OptBuilder<'a> {
    s: Switched<'a>,
}

fn longs<'a>(flags: &'a [FlagBuilder<'a>], opts: &'a [OptBuilder<'a>]) -> Vec<&'a str> {
    let all = flags
        .iter()
        .filter_map(|f| f.s.long)
        .chain(opts.iter().filter_map(|o| o.s.long));
    all.collect()
}

fn main() {
    let flags = [FlagBuilder { s: Switched { long: Some("verbose") } }, FlagBuilder { s: Switched { long: None } }];
    let opts = [OptBuilder { s: Switched { long: Some("output") } }];
    assert_eq!(longs(&flags, &opts), vec!["verbose", "output"]);
}
