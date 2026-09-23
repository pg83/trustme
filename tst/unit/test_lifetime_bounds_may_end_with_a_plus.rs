// async-trait writes `where 'a: 'async_trait + { .. }`. Upstream's
// `parse_lt_param_bounds` reads lifetimes while there are any and eats a `+`
// after each, so a trailing `+` - or no bound at all - is accepted, in a
// where clause as in a parameter list.
fn first<'a, 'b: 'a +>(x: &'a str, _y: &'b str) -> &'a str
where
    'b: 'a +,
    'a:,
{
    x
}

struct Holder<'a, 'b: 'a +>(&'a str, &'b str);

fn main() {
    let h = Holder("x", "y");
    assert_eq!(first(h.0, h.1), "x");
}
