//@ edition: 2015
// `?` right after `$(..)` is the repetition operator in every edition, on the
// expansion side as well as in the pattern. What 2018 changed is that `?` may no
// longer be a separator.
macro_rules! add_exprs {
    ($($e:expr)?) => (0 $(+ $e)?)
}

macro_rules! at_most_one {
    ($($a:ident)? ; $num:expr) => {{
        let mut total = 0;
        $(
            total += $a;
        )?
        total + $num
    }};
}

fn main() {
    assert_eq!(add_exprs!(), 0);
    assert_eq!(add_exprs!(7), 7);

    let five = 5;
    assert_eq!(at_most_one!(five ; 1), 6);
    assert_eq!(at_most_one!(; 1), 1);
}
