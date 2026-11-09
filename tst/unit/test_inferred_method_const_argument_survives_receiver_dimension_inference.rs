#![feature(generic_arg_infer)]

struct Matrix<const ROWS: usize, const COLS: usize>;

impl Matrix<13, 17> {
    fn with_depth<const DEPTH: usize>(&self, _: [u8; DEPTH]) -> (usize, usize, usize) {
        (13, 17, DEPTH)
    }
}

fn main() {
    let matrix = Matrix::<_, _>;
    assert_eq!(matrix.with_depth::<_>([0; 19]), (13, 17, 19));
}
