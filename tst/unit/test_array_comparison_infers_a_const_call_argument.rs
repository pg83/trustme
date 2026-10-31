#![feature(generic_arg_infer)]
fn pairs<const N: usize>(values: &[u16]) -> &[[u16; N]] { values.as_chunks().0 }
fn main() {
    let values = [2, 3, 5, 7, 11, 13];
    let result = pairs::<_>(&values);
    assert_eq!(result, &[[2u16, 3], [5, 7], [11, 13]]);
}
