fn longer<'a>(left: &'a str, right: &'a str) -> &'a str {
    if left.len() >= right.len() { left } else { right }
}
fn main() {
    let choose: for<'a> fn(&'a str, &'a str) -> &'a str = longer;
    let left = String::from("rust");
    let right = String::from("compiler");
    let selected: &'_ str = choose(&left, &right);
    assert_eq!(selected, "compiler");
    assert_eq!(choose("longer", "short"), "longer");
}
