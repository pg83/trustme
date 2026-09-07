// `map_windows(|[x, y]| ..)`: the closure's parameter is `&[char; N]` with `N`
// an inference variable settled by the pattern, so the closure's signature - part
// of the `FnMut` goal about it (upstream `ClosureArgs`) - carries a value
// variable as well as type variables.  Both are goal inputs, and the response
// must hand back the caller's variables for both; a value slot left canonical
// in the response was read as a variable of the caller's table.
//
// Same shape as the doctest at library/core/src/iter/traits/iterator.rs:1572.
#![feature(iter_map_windows)]

fn main() {
    let strings = "abcd".chars().map_windows(|[x, y]| format!("{}+{}", x, y)).collect::<Vec<String>>();
    assert_eq!(strings, vec!["a+b", "b+c", "c+d"]);
}
