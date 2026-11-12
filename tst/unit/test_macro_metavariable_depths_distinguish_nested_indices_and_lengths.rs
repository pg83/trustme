#![feature(macro_metavar_expr)]

macro_rules! coordinates {
    ($([$($value:expr),*]),*) => {
        [$([
            $(($value, ${index()}, ${index(1)}, ${len()}, ${len(1)})),*
        ]),*]
    };
}

fn main() {
    let actual: [[(u16, usize, usize, usize, usize); 3]; 2] =
        coordinates!([733, 739, 743], [751, 757, 761]);
    assert_eq!(actual, [
        [(733, 0, 0, 3, 2), (739, 1, 0, 3, 2), (743, 2, 0, 3, 2)],
        [(751, 0, 1, 3, 2), (757, 1, 1, 3, 2), (761, 2, 1, 3, 2)],
    ]);
}
