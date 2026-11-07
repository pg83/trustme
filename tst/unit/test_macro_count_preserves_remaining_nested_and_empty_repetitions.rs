#![feature(macro_metavar_expr)]

macro_rules! total {
    ($( $item:ident ),*) => { ${count($item)} };
}

macro_rules! grouped {
    ($( $group:ident: [$( $item:ident ),*] );+) => {
        (
            ${count($group)},
            ${count($item, 0)},
            ${count($item, 1)},
            [$( (${index()}, stringify!($group), ${count($item)}) ),+],
        )
    };
}

fn main() {
    assert_eq!(total!(), 0);
    assert_eq!(total!(only), 1);
    assert_eq!(total!(first, second, third), 3);
    assert_eq!(
        grouped!(wide: [a, b, c]; empty: []; pair: [d, e]),
        (3, 5, 3, [(0, "wide", 3), (1, "empty", 0), (2, "pair", 2)]),
    );
}
