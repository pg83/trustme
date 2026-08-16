// A `$vis` fragment stands wherever a visibility may be written, and expands to
// nothing when it is empty. A trait item accepted a literal `pub` but not the
// fragment, so the same macro worked in an impl and not in a trait.
//
// Same shape as the Rust Reference example items/traits.md:342.
macro_rules! createMethod {
    ($vis:vis $name:ident) => {
        $vis fn $name(&self) -> u32 {
            1
        }
    };
}

trait T1 {
    // An empty visibility.
    createMethod! { methodOfT1 }
}

struct S;

impl S {
    // A visibility is allowed here.
    createMethod! { pub methodOfS }
}

impl T1 for S {}

fn main() {
    let s = S;
    assert_eq!(s.methodOfT1(), 1);
    assert_eq!(s.methodOfS(), 1);
}
