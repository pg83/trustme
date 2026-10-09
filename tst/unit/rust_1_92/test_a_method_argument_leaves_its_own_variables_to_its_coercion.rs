// `let first_element = &first_sequence[best_i]` with `best_i` still an
// untyped variable, then `self.qformat(&first_element, ..)` into `&str`
// (difflib). rustc has the index's output by the time it checks the call.
// Our method lookup related the argument to the parameter through
// `evaluateCoercionGoal` and kept what that bound of the argument's own
// variables: the index output became `str`, and `SliceIndex<[&str]>` for
// `usize` had no such output. An argument whose relation fixes its own
// variables now hands the coercion to the argument-bindings phase, as the
// equality path already did.
struct Differ;

impl Differ {
    fn qformat(&self, first_line: &str, second_line: &str) -> Vec<String> {
        vec![first_line.to_string(), second_line.to_string()]
    }

    fn fancy_replace(&self, first_sequence: &[&str], second_sequence: &[&str]) -> Vec<String> {
        let mut res = Vec::new();
        let (mut best_i, mut best_j) = (0, 0);
        for i in 0..first_sequence.len() {
            for j in 0..second_sequence.len() {
                if first_sequence[i].len() == second_sequence[j].len() {
                    best_i = i;
                    best_j = j;
                }
            }
        }
        let first_element = &first_sequence[best_i];
        let second_element = &second_sequence[best_j];
        let chars: Vec<char> = first_element.chars().collect();
        if chars.len() > 1 {
            res.extend(self.qformat(&first_element, &second_element).iter().cloned());
        } else {
            let mut s = String::from("  ");
            s.push_str(&first_element);
            res.push(s);
        }
        res
    }
}

fn main() {
    assert_eq!(Differ.fancy_replace(&["ab", "c"], &["xy"]), ["ab", "xy"]);
}
