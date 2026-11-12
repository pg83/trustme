#![feature(macro_metavar_expr)]

macro_rules! inspect_counts {
    ($( [$($value:ident),*] );*) => {
        (
            ${count($value)},
            ${count($value, 0)},
            ${count($value, 1)},
            [$( (${count($value)}, ${count($value, 0)}) ),*],
        )
    };
}

macro_rules! count_flat {
    ($($value:ident),*) => { ${count($value, 0)} };
}

fn main() {
    assert_eq!(inspect_counts!([first, second]; [third, fourth, fifth]), (5, 5, 2, [(2, 2), (3, 3)]));
    assert_eq!(count_flat!(), 0);
}
