/* clap_builder's `als.join(&val_sep)`: `als` is a `Vec::new()` typed by `extend` with
   `String`s, `val_sep: String`; `[String]: Join<?S>` has one viable impl
   (`Join<&str>`), which upstream selects while resolving the argument's target
   (`resolve_vars_with_obligations`), so `&String` deref-coerces to `&str`. */
struct Arg {
    short_aliases: Vec<(char, bool)>,
    aliases: Vec<(&'static str, bool)>,
}
fn pluralize(n: usize, one: &str, many: &str) -> &'static str {
    if n == 1 { let _ = one; "" } else { let _ = many; "es" }
}
fn spec_vals(a: &Arg) -> String {
    let ctx = "<";
    let val_sep = format!("{ctx}, {ctx}");
    let mut als = Vec::new();
    let short_als = a
        .short_aliases
        .iter()
        .filter(|&als| als.1)
        .map(|als| format!("{ctx}-{}{ctx}", als.0));
    als.extend(short_als);
    let long_als = a
        .aliases
        .iter()
        .filter(|&als| als.1)
        .map(|als| format!("{ctx}--{}{ctx}", als.0));
    als.extend(long_als);
    if !als.is_empty() {
        let plural = pluralize(als.len(), "", "es");
        let als = als.join(&val_sep);
        format!("[alias{plural}: {als}]")
    } else {
        String::new()
    }
}
fn main() {
    let a = Arg { short_aliases: vec![('a', true), ('b', false)], aliases: vec![("long", true)] };
    assert_eq!(spec_vals(&a), "[aliases: <-a<<, <<--long<]");
}
