// yansi's `define_property!`: an outer macro's `$V:path` fragment is
// transcribed into the body of a nested `macro_rules!` definition, and the
// nested macro later passes it on to a continuation macro. Upstream
// transcribes a `path` fragment as the path's own tokens (an invisible
// group around `TokenStream::from_ast(path)`), so the nested definition
// simply holds the path.
#[derive(Debug, PartialEq)]
enum Color {
    Primary,
    Fixed(u8),
}

macro_rules! define_property {
    ([$d:tt] $kind:ident { $($prop:ident => $V:path $([$($a:tt)*])?),* $(,)? }) => {
        macro_rules! $kind {
            ($d cont:ident) => (
                vec![ $( $d cont!($prop => $V $([$($a)*])?) ),* ]
            )
        }
    };
    ($($t:tt)*) => { define_property!([$] $($t)*); }
}

macro_rules! named {
    ($prop:ident => $V:path) => { (stringify!($prop), $V) };
    ($prop:ident => $V:path [$a:expr]) => { (stringify!($prop), $V($a)) };
}

define_property! {
    fg {
        primary => Color::Primary,
        fixed => Color::Fixed[8],
    }
}

fn main() {
    let v = fg!(named);
    assert_eq!(v, vec![("primary", Color::Primary), ("fixed", Color::Fixed(8))]);
}
