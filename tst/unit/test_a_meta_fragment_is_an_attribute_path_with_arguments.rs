// lalrpop's `lalrpop_mod!(#[rustfmt::skip] #[allow(unused_parens)] parser)`
// matches `$(#[$attr:meta])*`. A `meta` fragment is an attribute item: an
// optional `unsafe(..)` around a simple path - `::`-separated, maybe with a
// leading `::` - and its arguments; the matcher took one identifier only.
macro_rules! attrs {
    ($(#[$attr:meta])* $vis:vis $name:ident) => {
        attrs!($(#[$attr])* $vis $name, concat!("/", stringify!($name), ".rs"));
    };
    ($(#[$attr:meta])* $vis:vis $name:ident, $source:expr) => {
        $(#[$attr])* $vis mod $name {
            pub const SOURCE: &str = $source;
        }
    };
}

attrs!(#[rustfmt::skip] #[allow(unused_parens)] parser);
attrs!(#[rustfmt::skip::attributes(attrs)] pub(crate) plain);

macro_rules! meta_text {
    ($($m:meta),*) => {
        [$(stringify!($m)),*]
    };
}

fn main() {
    assert_eq!(parser::SOURCE, "/parser.rs");
    assert_eq!(plain::SOURCE, "/plain.rs");
    assert_eq!(
        meta_text!(rustfmt::skip, a::b(c), a::b = 1, ::a, unsafe(no_mangle), allow(x)),
        ["rustfmt::skip", "a::b(c)", "a::b = 1", "::a", "unsafe(no_mangle)", "allow(x)"]
    );
}
