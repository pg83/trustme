//@ run-pass
/* clap_builder's `EnumValueParser::parse_ref`: `E::value_variants().iter().filter_map(|v|
   v.to_possible_value()).filter(|v| !v.is_hide_set()).collect::<Vec<_>>()`.  The `Vec<_>` is
   `FromIterator<<Filter<FilterMap<Iter<E>, C1>, C2> as Iterator>::Item>`, and the item is the
   `B` of `impl<B, I, F> Iterator for FilterMap<I, F> where F: FnMut(I::Item) -> Option<B>` -
   a fresh variable that the where-clause decides.  Normalized first inside the solver, for the
   `Filter` impl's `P: FnMut(&I::Item) -> bool`, where nothing took the obligation, that
   variable was made, cached, and never bound, and the checker's `_` stayed unknown ("Spare
   rules left after typecheck stabilised").  toml's `Display for Table` (`tables.next()` on the
   same `filter_map(..).filter(..)` chain, then `table.fmt(f)?`) lost the `?`'s `Try` the same way. */
pub struct PossibleValue {
    hide: bool,
}

impl PossibleValue {
    pub fn is_hide_set(&self) -> bool {
        self.hide
    }
}

pub trait ValueEnum: Sized + Clone {
    fn value_variants<'a>() -> &'a [Self];
    fn to_possible_value(&self) -> Option<PossibleValue>;
}

#[derive(Clone)]
struct Color(bool);

impl ValueEnum for Color {
    fn value_variants<'a>() -> &'a [Self] {
        &[Color(false), Color(true), Color(false)]
    }
    fn to_possible_value(&self) -> Option<PossibleValue> {
        Some(PossibleValue { hide: self.0 })
    }
}

fn possible_values<E: ValueEnum>() -> usize {
    E::value_variants()
        .iter()
        .filter_map(|v| v.to_possible_value())
        .filter(|v| !v.is_hide_set())
        .collect::<Vec<_>>()
        .len()
}

struct Table(u32);

impl std::fmt::Display for Table {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "t{}", self.0)
    }
}

struct Buffer(Vec<Option<Table>>);

fn required(t: &Table) -> bool {
    t.0 > 0
}

impl std::fmt::Display for Buffer {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut tables = self.0.iter().filter_map(|t| t.as_ref()).filter(|t| required(t));
        if let Some(table) = tables.next() {
            table.fmt(f)?;
        }
        Ok(())
    }
}

fn main() {
    assert_eq!(possible_values::<Color>(), 2);
    assert_eq!(Buffer(vec![None, Some(Table(0)), Some(Table(7))]).to_string(), "t7");
    let n = [Color(true)].iter().filter_map(|v| v.to_possible_value()).filter(|v| v.is_hide_set()).collect::<Vec<_>>().len();
    assert_eq!(n, 1);
}
