// A closure is Copy only when everything it captures is. Whether a closure
// captures anything is worked out after typecheck, so the solver answers Copy
// for every closure; the pass that does work it out asked the solver anyway.
// A closure holding a String was therefore taken for Copy, so a closure that
// consumes it captured it by reference - and moving out through a reference is
// not something MIR can express.

fn main() {
    let owned = String::from("captured");
    let inner = move |()| owned;
    let outer = || inner(());
    assert_eq!(outer(), "captured");
}
