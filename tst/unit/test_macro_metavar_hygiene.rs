// Two metavariables can share a name and still be distinct: what tells them
// apart is the expansion each was written in.
macro_rules! make_mac {
    ( $($dollar:tt $arg:ident),+ ) => {
        macro_rules! mac {
            ( $($dollar $arg : ident),+ ) => {
                $( $dollar $arg )-+
            }
        }
    };
}

macro_rules! show_hygiene {
    ( $dollar:tt $arg:ident ) => {
        make_mac!($dollar $arg, $dollar arg);
    };
}

show_hygiene!( $arg );

fn main() {
    let x = 5;
    let y = 3;
    assert_eq!(mac!(x, y), 2);
}
