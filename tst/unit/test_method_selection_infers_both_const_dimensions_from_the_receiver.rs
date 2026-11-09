#![feature(generic_arg_infer)]

struct Window<const ROWS: usize, const COLS: usize>;

trait Dimensions {
    fn dimensions(&self) -> (usize, usize);
}

impl Dimensions for Window<7, 11> {
    fn dimensions(&self) -> (usize, usize) {
        (7, 11)
    }
}

fn main() {
    let window = Window::<_, _>;
    assert_eq!(window.dimensions(), (7, 11));
}
