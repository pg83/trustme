// `${count($x, depth)}` counts the repetitions `depth` levels above the values
// rather than the values; `${index(d)}` and `${len(d)}` name the loop `d`
// levels out from the innermost one. `${ignore($x)}` expands to nothing and
// only says which variable drives the repetition it is in, so a variable that
// repeats deeper than there still names one.
#![feature(macro_metavar_expr)]

macro_rules! count_idents {
    ( $( $i:ident ),* ) => { ${count($i)} };
}

macro_rules! count_idents_2 {
    ( $( [ $( $i:ident ),* ] ),* ) => { ${count($i)} };
}

macro_rules! count_depths {
    ( $( { $( [ $( $outer:ident : ( $( $inner:ident )* ) )* ] )* } )* ) => {
        (
            ${count($inner, 0)},
            ${count($inner, 1)},
            ${count($inner, 2)},
            ${count($inner, 3)},
        )
    };
}

macro_rules! enumerate_literals_2 {
    ( $( [ $( ($l:literal) ),* ] ),* ) => {
        [ $( $( (${index(1)}, ${len(1)}, ${index(0)}, ${len(0)}, $l), )* )* ]
    };
}

macro_rules! ignore_deeper {
    ( $( [ $( ( $( $i:ident )* ) )* ] ),* ) => {
        [ $( ${ignore($i)} ${count($i, 0)}, )* ]
    };
}

fn main() {
    assert_eq!(count_idents!(a, b, c), 3);
    assert_eq!(count_idents_2!([a, b, c], [d, e]), 5);
    assert_eq!(
        count_depths! {
            { [ A: (a b c) D: (d e f) ] [ G: (g h) ] }
            { [ O: (o) P: (p q) ] }
        },
        (11, 5, 3, 2)
    );
    assert_eq!(
        enumerate_literals_2![[("foo"), ("bar")], [("baz")]],
        [(0, 2, 0, 2, "foo"), (0, 2, 1, 2, "bar"), (1, 2, 0, 1, "baz")]
    );
    assert_eq!(ignore_deeper![[(a b) (c)], [(d e f)]], [3, 3]);
}
