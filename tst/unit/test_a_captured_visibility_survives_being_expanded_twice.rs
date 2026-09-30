// tower-http's `pin_project_cfg!` captures `$vis:vis`, carries it inside a
// `$outer:tt` group and expands that group twice - once for each `cfg` of
// the enum it builds. Expanding a token twice copies it, and copying a
// captured visibility was the one fragment kind the token copy did not
// know: "Fragment with invalid token type (TOK_INTERPOLATED_VIS)".
macro_rules! twice {
    ($(#[$attr:meta])* $vis:vis enum $name:ident { $($variant:ident),* $(,)? }) => {
        twice! { @emit [$(#[$attr])* $vis enum] $name { $($variant),* } }
    };
    (@emit $outer:tt $name:ident $body:tt) => {
        twice! { @one #[cfg(all())] $outer $name $body }
        twice! { @one #[cfg(any())] $outer $name $body }
    };
    (@one #[$cfg:meta] [$($head:tt)*] $name:ident $body:tt) => {
        #[$cfg]
        $($head)* $name $body
    };
}

mod m {
    twice! {
        #[derive(Debug, PartialEq)]
        pub(crate) enum State { Idle, Busy }
    }
}

fn main() {
    assert_eq!(format!("{:?}", m::State::Busy), "Busy");
    assert_ne!(m::State::Idle, m::State::Busy);
}
