// `..` is a pattern in its own right: a macro may take it as a `pat` fragment,
// and a slice pattern with a second one parses (only using it is an error).
macro_rules! accept_pat {
    ($p:pat) => {
        stringify!($p)
    };
}

#[allow(unreachable_patterns)]
macro_rules! first_of {
    ($slice:expr, $p:pat => $v:expr) => {
        match $slice {
            $p => Some($v),
            #[allow(unreachable_patterns)]
            _ => None,
        }
    };
}

#[cfg(false)]
fn never_compiled(xs: [u8; 3]) {
    let [.., ..] = xs;
    let (.., ..) = (1, 2);
    // The rest may even be written as an alternative of an or-pattern.
    let [A | B, .. | ..] = xs;
}

fn main() {
    assert_eq!(accept_pat!(..), "..");

    let xs = [1u8, 2, 3];
    assert_eq!(first_of!(xs, [first, ..] => first), Some(1));
    assert_eq!(first_of!(xs, [.., last] => last), Some(3));
    assert_eq!(first_of!(xs, [_, mid @ .., _] => mid.len()), Some(1));
}
