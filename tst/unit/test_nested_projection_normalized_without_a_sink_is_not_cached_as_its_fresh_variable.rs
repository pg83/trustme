// A projection normalized on the way of a sink-less normalization
// (`<Filter<FilterMap<..>> as Iterator>::Item` is `<FilterMap<..> as Iterator>::Item`) has the
// probe standing in for the sink and must not cache its fresh impl variable as the normalized
// form: a goal reading such an entry bound the checker's variable to a canonical one of its own,
// and the `map(|(a, _)| ..)` closure's parameter never met the chain's item.
// (rstest_macros `error.rs:123`)
pub trait ToTokens {
    fn text(&self) -> String;
}

pub struct FnArg {
    name: String,
    pat: Option<String>,
}

impl ToTokens for &FnArg {
    fn text(&self) -> String {
        self.name.clone()
    }
}

impl FnArg {
    fn maybe_pat(&self) -> Option<&String> {
        self.pat.as_ref()
    }
}

pub struct Error(String);

impl Error {
    pub fn new_spanned<T: ToTokens>(tokens: T, message: &str) -> Error {
        Error(format!("{}: {}", tokens.text(), message))
    }
}

fn is_implicit(p: &String) -> bool {
    p.starts_with('_')
}

fn destruct_errors<'a>(inputs: &'a [FnArg]) -> Box<dyn Iterator<Item = Error> + 'a> {
    Box::new(
        inputs
            .iter()
            .filter_map(|a| a.maybe_pat().map(|p| (a, p)))
            .filter(|&(_, p)| p.len() > 1)
            .filter(|&(_, p)| is_implicit(p))
            .map(|(a, _)| Error::new_spanned(a, "destructured without from")),
    )
}

fn main() {
    let inputs = [FnArg { name: "x".into(), pat: Some("_hidden".into()) }, FnArg { name: "y".into(), pat: None }];
    let errors: Vec<String> = destruct_errors(&inputs).map(|e| e.0).collect();
    assert_eq!(errors, vec!["x: destructured without from"]);
}
