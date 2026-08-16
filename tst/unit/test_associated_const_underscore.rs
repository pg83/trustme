// `const _: () = ();` parses wherever a const item does, associated items
// included — rustc rejects it in a trait or impl afterwards, which a `cfg`
// removes the item before reaching. Both bodies demanded a name.
//
// Same shape as the ui test
// parser/assoc/assoc-const-underscore-syntactic-pass.rs.
#[cfg(FALSE)]
trait Stripped {
    const _: () = ();
}

struct S;

#[cfg(FALSE)]
impl S {
    const _: () = ();
}

// At item level it is real, and keeps working.
const _: () = ();

fn main() {
    let _ = S;
}
