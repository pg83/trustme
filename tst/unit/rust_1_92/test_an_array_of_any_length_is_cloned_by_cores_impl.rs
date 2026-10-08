// An array is `Clone` through core's
// `impl<T: Clone, const N: usize> Clone for [T; N]`, at any length: rustc's
// selection has no builtin `Clone` for `ty::Array` ("Implementations
// provided in libcore", `copy_clone_conditions`). ruzstd clones a
// `[SymbolStates; 256]` of non-`Copy` states.
#[derive(Clone, Debug, PartialEq)]
struct State(String);

fn main() {
    let states: [State; 300] = std::array::from_fn(|i| State(i.to_string()));
    let copy = states.clone();
    assert_eq!(copy[299], State(String::from("299")));
    assert_eq!(copy, states);
    let pairs: [(u8, State); 2] = [(1, State(String::from("a"))), (2, State(String::from("b")))];
    assert_eq!(pairs.clone()[1].1, State(String::from("b")));
}
