/* clap 2's `get_required_usage_from`: `for r in rl { if !reqs.contains(&r) { v.push(r); } }`
   with `rl` from `.map(|g| g.requires.as_ref().unwrap())` - `map`'s output `B` is open,
   so the closure's body is what decides it, and `rl: &Vec<&str>` is known before the
   loop's `Item` reaches `push`; `r` is `&&str` and `push` coerces it to `&str`, rather
   than the loop's `Item` being read off `push`'s parameter. */
struct Group<'a> {
    name: String,
    requires: Option<Vec<&'a str>>,
}

fn required<'a>(groups: &[Group<'a>], name: &str, reqs: &[&'a str]) -> Vec<&'a str> {
    let mut new_reqs: Vec<&str> = vec![];
    if let Some(rl) = groups
        .iter()
        .filter(|g| g.requires.is_some())
        .find(|g| g.name == name)
        .map(|g| g.requires.as_ref().unwrap())
    {
        for r in rl {
            if !reqs.contains(&r) {
                new_reqs.push(r);
            }
        }
    }
    new_reqs
}

fn main() {
    let groups = [Group { name: "g".to_string(), requires: Some(vec!["a", "b", "c"]) }];
    assert_eq!(required(&groups, "g", &["b"]), vec!["a", "c"]);
}
