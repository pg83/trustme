//@ compile-fail: `$x:expr` may be followed by `$t:ty`, which is not allowed for `expr` fragments
// The last entry of a repetition is followed by the separator between two
// iterations and by whatever follows the repetition after the last one, so a
// rule that puts a type straight after a repeated expression is as ambiguous
// as writing the two side by side.

macro_rules! after_loop {
    ($($x:expr),* $t:ty) => {
        $t
    };
}

fn main() {}
