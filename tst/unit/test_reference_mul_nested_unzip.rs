fn main() {
    let input = vec![1, 2, 3, 4, 5];
    let (indexes, (squares, cubes)): (Vec<_>, (Vec<_>, Vec<_>)) = input
        .iter()
        .map(|x| (x * x, x * x * x))
        .enumerate()
        .unzip();

    assert_eq!(indexes, vec![0, 1, 2, 3, 4]);
    assert_eq!(squares, vec![1, 4, 9, 16, 25]);
    assert_eq!(cubes, vec![1, 8, 27, 64, 125]);
}
