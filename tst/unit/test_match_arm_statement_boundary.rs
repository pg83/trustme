//@ run-pass
// A block-headed expression ends the statement it heads, so in a match arm the
// `|` that follows one starts a new arm with a leading vert rather than a
// `BitOr`.  The rule covers every block form, and an `:expr` fragment standing
// for one behaves the way the tokens behind it would.

#![allow(unused_braces, unused_unsafe)]

#[derive(Copy, Clone)]
struct X;

impl std::ops::BitOr<X> for &str {
    type Output = ();

    fn bitor(self, _: X) {}
}

macro_rules! boundary {
    ($e:expr) => {
        () = match X {
            X if false => $e | X if false => {}
            X => {}
        };
    };
}

fn main() {
    let x = X;

    () = match x {
        X if false => unsafe {} | X if false => {}
        X if false => while false {} | X if false => {}
        X if false => loop { break; } | X if false => {}
        X if false => for _ in 0..0 {} | X if false => {}
        X if false => const {} | X if false => {}
        X if false => if true {} | X if false => {}
        X if false => match () { () => {} } | X if false => {}
        X if false => { () } | X if false => {}

        // No block heads these, so the `|` is a `BitOr`.
        X if false => "" | X,

        X => {}
    };

    boundary!(if true {});
    boundary!(unsafe {});

    // The same rule in statement position: `|x| x` is a closure of its own.
    let _: fn(X) -> X = { unsafe {} |x| x };
    let _: fn(X) -> X = { const {} |x| x };
    macro_rules! head {
        ($e:expr) => {
            let _: fn(X) -> X = { $e |x| x };
        };
    }
    head!(if true {});
}
